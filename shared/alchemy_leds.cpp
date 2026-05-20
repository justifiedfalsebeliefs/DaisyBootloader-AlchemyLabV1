#pragma GCC optimize("Os")
#include "alchemy_leds.h"
#include "stm32h7xx_hal.h"
#include <cstring>

// ──────────────────────────────────────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────────────────────────────────────

// TIM3 at 240 MHz (SYSCLK=480→HCLK÷2=240→APB1÷2=120→TIM3=2×120=240 MHz)
// 800 kHz WS2812: period=300 ticks
static constexpr uint32_t kPeriod = 300;
static constexpr uint32_t kBit0   = 96;   // 32 % → T0H ≈ 400 ns
static constexpr uint32_t kBit1   = 192;  // 64 % → T1H ≈ 800 ns

static constexpr uint16_t kNumLeds   = 102;
static constexpr uint16_t kResetLen  = 50;
static constexpr uint16_t kDmaBufLen = kNumLeds * 24u + kResetLen;  // 2498

// Ring chain-start indices from kAlchemyLabV1Layout
static constexpr uint8_t kRingStart[6] = {69, 86, 52, 35, 17, 0};
static constexpr uint8_t kLedsPerRing  = 16;
static constexpr uint8_t kNumRings     = 6;
static constexpr uint32_t kRevMs       = 2000;

// Warm-white at ~5 % brightness, WS2812 GRB order
static constexpr uint8_t kColG = 5, kColR = 10, kColB = 2;

// ──────────────────────────────────────────────────────────────────────────────
// State (file-scope to avoid class overhead)
// ──────────────────────────────────────────────────────────────────────────────

static uint8_t  s_frame[kNumLeds * 3];

// DMA buffer in D2 RAM so DMA1 can reach it without going through AXI.
static uint32_t s_dma_buf[kDmaBufLen] __attribute__((section(".sram1_bss")));

static DMA_HandleTypeDef s_hdma;
static volatile bool     s_busy;

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────

static void OnDmaComplete(DMA_HandleTypeDef*) { s_busy = false; }

static void BuildDmaBuf() {
    uint16_t idx = 0;
    for (uint16_t led = 0; led < kNumLeds; ++led) {
        for (int b = 0; b < 3; ++b) {
            uint8_t val = s_frame[led * 3 + b];
            for (int bit = 7; bit >= 0; --bit)
                s_dma_buf[idx++] = (val >> bit) & 1u ? kBit1 : kBit0;
        }
    }
    while (idx < kDmaBufLen) s_dma_buf[idx++] = 0u;
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

namespace alchemy {

DMA_HandleTypeDef& LedGetHdma() { return s_hdma; }

void LedInit() {
    memset(s_frame, 0, sizeof(s_frame));
    s_busy = false;

    // ── Clocks ────────────────────────────────────────────────────────────────
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // ── PC9 → TIM3_CH4 AF2 ───────────────────────────────────────────────────
    // HAL_GPIO_Init is already in the binary via libDaisy GPIO usage.
    GPIO_InitTypeDef gpio{};
    gpio.Pin       = GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOC, &gpio);

    // ── TIM3 via direct registers — avoids pulling in the HAL TIM module ─────
    // CH4 = PWM Mode 1 (high while CNT < CCR4); Update event drives DMA.
    TIM3->CR1   = 0;
    TIM3->PSC   = 0;
    TIM3->ARR   = kPeriod - 1u;            // 299 → 800 kHz
    TIM3->CCR4  = 0;
    TIM3->CCMR2 = 6u << TIM_CCMR2_OC4M_Pos; // PWM Mode 1, CC4S=output
    TIM3->CCER  = TIM_CCER_CC4E;             // CH4 output enable, active-high
    TIM3->DIER  = TIM_DIER_UDE;              // DMA request on Update event
    TIM3->EGR   = TIM_EGR_UG;               // Apply PSC/ARR immediately
    TIM3->CR1   = TIM_CR1_CEN;              // Run counter

    // ── DMA1_Stream7 → DMAMUX TIM3_UP (request 27) ───────────────────────────
    // HAL_DMA_Init and HAL_DMA_Start_IT are already in the binary via SAI DMA.
    s_hdma.Instance                 = DMA1_Stream7;
    s_hdma.Init.Request             = DMA_REQUEST_TIM3_UP;
    s_hdma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    s_hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    s_hdma.Init.Mode                = DMA_NORMAL;
    s_hdma.Init.Priority            = DMA_PRIORITY_HIGH;
    s_hdma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&s_hdma);

    s_hdma.XferCpltCallback  = OnDmaComplete;
    s_hdma.XferErrorCallback = nullptr;

    HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
}

void LedUpdate(uint32_t t_ms) {
    if (s_busy) return;

    memset(s_frame, 0, sizeof(s_frame));

    for (uint8_t ring = 0; ring < kNumRings; ++ring) {
        uint32_t phase_ms = (t_ms + ring * (kRevMs / kNumRings)) % kRevMs;
        uint8_t  led_off  = static_cast<uint8_t>((phase_ms * kLedsPerRing) / kRevMs);
        uint16_t chain    = kRingStart[ring] + led_off;
        s_frame[chain * 3 + 0] = kColG;
        s_frame[chain * 3 + 1] = kColR;
        s_frame[chain * 3 + 2] = kColB;
    }

    BuildDmaBuf();
    s_busy = true;
    // Write each DMA value to TIM3->CCR4; the Update event (800 kHz) clocks it.
    HAL_DMA_Start_IT(&s_hdma,
                     reinterpret_cast<uint32_t>(s_dma_buf),
                     reinterpret_cast<uint32_t>(&TIM3->CCR4),
                     kDmaBufLen);
}

} // namespace alchemy

// DMA1_Stream7 is unused by libDaisy — safe to define here.
extern "C" void DMA1_Stream7_IRQHandler(void) {
    HAL_DMA_IRQHandler(&alchemy::LedGetHdma());
}
