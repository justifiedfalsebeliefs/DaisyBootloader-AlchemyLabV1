#include "alchemy_leds.h"
#include "stm32h7xx_hal.h"
#include <cstring>

// ──────────────────────────────────────────────────────────────────────────────
// Hardware constants
// ──────────────────────────────────────────────────────────────────────────────

// TIM3 PWM at 240 MHz TIM clock → 800 kHz WS2812
static constexpr uint32_t kPeriod = 300;   // ARR = kPeriod-1 = 299
static constexpr uint32_t kBit0   = 96;    // T0H ≈ 400 ns  (32 % duty)
static constexpr uint32_t kBit1   = 192;   // T1H ≈ 800 ns  (64 % duty)

static constexpr uint16_t kNumLeds    = 102;
static constexpr uint16_t kResetLen   = 50;   // ≥50 × 1.25 µs = 62.5 µs latch
static constexpr uint16_t kDmaBufLen  = kNumLeds * 24u + kResetLen;  // 2498

// ──────────────────────────────────────────────────────────────────────────────
// Animation constants
// ──────────────────────────────────────────────────────────────────────────────

// Chain-start index for each of the 6 pot rings (from kAlchemyLabV1Layout).
static constexpr uint8_t kRingStart[6] = {69, 86, 52, 35, 17, 0};
static constexpr uint8_t kLedsPerRing  = 16;
static constexpr uint8_t kNumRings     = 6;

// Spinning animation: one full revolution per kRevMs milliseconds.
static constexpr uint32_t kRevMs = 2000;

// Colour at very low brightness: warm-white at ~5 % of full scale.
// WS2812 byte order is G, R, B.
static constexpr uint8_t kColG = 5;
static constexpr uint8_t kColR = 10;
static constexpr uint8_t kColB = 2;

// ──────────────────────────────────────────────────────────────────────────────
// Module-level state
// ──────────────────────────────────────────────────────────────────────────────

// GRB frame buffer (regular SRAM — small, 306 bytes).
static uint8_t s_frame[kNumLeds * 3];

// DMA PWM buffer placed in D2 RAM (DMA-accessible, not cached).
// __attribute__ section maps to RAM_D2_DMA (0x30000000, 32 KB) in the
// boot_linker.lds .sram1_bss region.
static uint32_t s_dma_buf[kDmaBufLen] __attribute__((section(".sram1_bss")));

static TIM_HandleTypeDef s_htim;
static DMA_HandleTypeDef s_hdma;
static volatile bool     s_busy;

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────

static void BuildDmaBuf() {
    uint16_t idx = 0;
    for (uint16_t led = 0; led < kNumLeds; ++led) {
        for (int byte_i = 0; byte_i < 3; ++byte_i) {
            uint8_t val = s_frame[led * 3 + byte_i];
            for (int bit = 7; bit >= 0; --bit) {
                s_dma_buf[idx++] = (val >> bit) & 1u ? kBit1 : kBit0;
            }
        }
    }
    while (idx < kDmaBufLen) s_dma_buf[idx++] = 0u;  // reset guard
}

// ──────────────────────────────────────────────────────────────────────────────
// HAL callback — fires when DMA transfer completes (TIM3_CH4 DMA done)
// ──────────────────────────────────────────────────────────────────────────────

extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim) {
    if (htim->Instance == TIM3) {
        HAL_TIM_PWM_Stop_DMA(&s_htim, TIM_CHANNEL_4);
        s_busy = false;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

namespace alchemy {

DMA_HandleTypeDef& LedGetHdma() { return s_hdma; }

void LedInit() {
    memset(s_frame,   0, sizeof(s_frame));
    memset(s_dma_buf, 0, sizeof(s_dma_buf));
    s_busy = false;

    // ── Clocks ───────────────────────────────────────────────────────────────
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();  // also enables DMAMUX1 on H7
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // ── PC9 → TIM3_CH4 (AF2) ─────────────────────────────────────────────────
    GPIO_InitTypeDef gpio{};
    gpio.Pin       = GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOC, &gpio);

    // ── TIM3 PWM base ─────────────────────────────────────────────────────────
    s_htim.Instance               = TIM3;
    s_htim.Init.Prescaler         = 0;
    s_htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim.Init.Period            = kPeriod - 1u;  // 299
    s_htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&s_htim);

    TIM_OC_InitTypeDef oc{};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&s_htim, &oc, TIM_CHANNEL_4);

    // ── DMA1_Stream7 → DMAMUX TIM3_CH4 (request 26) ──────────────────────────
    s_hdma.Instance                 = DMA1_Stream7;
    s_hdma.Init.Request             = DMA_REQUEST_TIM3_CH4;
    s_hdma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    s_hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    s_hdma.Init.Mode                = DMA_NORMAL;
    s_hdma.Init.Priority            = DMA_PRIORITY_HIGH;
    s_hdma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&s_hdma);

    // Link DMA handle to TIM handle (channel 4 slot)
    __HAL_LINKDMA(&s_htim, hdma[TIM_DMA_ID_CC4], s_hdma);

    // DMA1_Stream7 is not used by libDaisy — safe to claim here.
    HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
}

void LedUpdate(uint32_t t_ms) {
    if (s_busy) return;  // previous frame still in flight

    // Clear frame
    memset(s_frame, 0, sizeof(s_frame));

    // Spinning single pip per ring, all rings phase-staggered.
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
    HAL_TIM_PWM_Start_DMA(&s_htim, TIM_CHANNEL_4, s_dma_buf, kDmaBufLen);
}

} // namespace alchemy

// ──────────────────────────────────────────────────────────────────────────────
// DMA1_Stream7 IRQ — not defined elsewhere in libDaisy
// ──────────────────────────────────────────────────────────────────────────────
extern "C" void DMA1_Stream7_IRQHandler(void) {
    HAL_DMA_IRQHandler(&alchemy::LedGetHdma());
}
