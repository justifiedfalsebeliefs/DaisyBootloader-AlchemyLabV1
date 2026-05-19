#pragma once
#include <cstdint>
#include "stm32h7xx_hal.h"

// Bootloader LED animation for Alchemy Lab V1.
// Drives the 102-LED WS2812 chain (6 pot rings × 16 + 6 button LEDs)
// via TIM3_CH4 (PC9, AF2) + DMA1_Stream7.
//
// Clock assumptions (libDaisy 480 MHz config):
//   SYSCLK=480 → HCLK÷2=240 → APB1÷2=120 → TIM3=2×120=240 MHz
//   ARR=299 → 800 kHz WS2812 protocol

namespace alchemy {

void LedInit();
void LedUpdate(uint32_t t_ms);  // compute frame + push to DMA (non-blocking)

// Called by DMA1_Stream7_IRQHandler — do not call directly.
DMA_HandleTypeDef& LedGetHdma();

} // namespace alchemy
