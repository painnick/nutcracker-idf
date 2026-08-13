/**
 * @file rccar_pins.h
 * @brief 핀 정의 (보드 변형별). 변형 선택은 Kconfig RCCAR_BOARD.
 *        설계: docs/superpowers/specs/2026-08-13-rccar-board-pin-variants-design.md
 */
#ifndef RCCAR_PINS_H
#define RCCAR_PINS_H

#include "sdkconfig.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_RCCAR_BOARD_KINGTIGER_1_1)

/* ===== kingtiger1.1 (현재 nutcracker1.0과 동일 값) ===== */

/* DRV8833 #1 전륜 */
#define RCCAR_PIN_FL_IN1 (GPIO_NUM_27)
#define RCCAR_PIN_FL_IN2 (GPIO_NUM_26)
#define RCCAR_PIN_FR_IN1 (GPIO_NUM_25)
#define RCCAR_PIN_FR_IN2 (GPIO_NUM_33)

/* DRV8833 #2 후륜 */
#define RCCAR_PIN_RL_IN1 (GPIO_NUM_32)
#define RCCAR_PIN_RL_IN2 (GPIO_NUM_14)
#define RCCAR_PIN_RR_IN1 (GPIO_NUM_13)
#define RCCAR_PIN_RR_IN2 (GPIO_NUM_16)

/* DRV8833 #3 포탑 */
#define RCCAR_PIN_TURRET_IN1 (GPIO_NUM_22)
#define RCCAR_PIN_TURRET_IN2 (GPIO_NUM_21)

/* 레이더 CR 서보 */
#define RCCAR_PIN_RADAR_SERVO (GPIO_NUM_17)

/* 웜 화이트 MOSFET */
#define RCCAR_PIN_WARM_WHITE (GPIO_NUM_4)

/* DFPlayer TX */
#define RCCAR_PIN_SOUND_TX (GPIO_NUM_5)
#define RCCAR_PIN_SOUND_RX (GPIO_NUM_NC)

/* 74HC595 x4 */
#define RCCAR_PIN_595_DATA  (GPIO_NUM_23)
#define RCCAR_PIN_595_CLOCK (GPIO_NUM_18)
#define RCCAR_PIN_595_LATCH (GPIO_NUM_19)

#elif defined(CONFIG_RCCAR_BOARD_NUTCRACKER_1_0)

/* ===== nutcracker1.0 ===== */

/* DRV8833 #1 전륜 */
#define RCCAR_PIN_FL_IN1 (GPIO_NUM_27)
#define RCCAR_PIN_FL_IN2 (GPIO_NUM_26)
#define RCCAR_PIN_FR_IN1 (GPIO_NUM_25)
#define RCCAR_PIN_FR_IN2 (GPIO_NUM_33)

/* DRV8833 #2 후륜 */
#define RCCAR_PIN_RL_IN1 (GPIO_NUM_32)
#define RCCAR_PIN_RL_IN2 (GPIO_NUM_14)
#define RCCAR_PIN_RR_IN1 (GPIO_NUM_13)
#define RCCAR_PIN_RR_IN2 (GPIO_NUM_16)

/* DRV8833 #3 포탑 */
#define RCCAR_PIN_TURRET_IN1 (GPIO_NUM_22)
#define RCCAR_PIN_TURRET_IN2 (GPIO_NUM_21)

/* 레이더 CR 서보 */
#define RCCAR_PIN_RADAR_SERVO (GPIO_NUM_17)

/* 웜 화이트 MOSFET */
#define RCCAR_PIN_WARM_WHITE (GPIO_NUM_4)

/* DFPlayer TX */
#define RCCAR_PIN_SOUND_TX (GPIO_NUM_5)
#define RCCAR_PIN_SOUND_RX (GPIO_NUM_NC)

/* 74HC595 x4 */
#define RCCAR_PIN_595_DATA  (GPIO_NUM_23)
#define RCCAR_PIN_595_CLOCK (GPIO_NUM_18)
#define RCCAR_PIN_595_LATCH (GPIO_NUM_19)

#else
#error "RCCAR board variant not selected (CONFIG_RCCAR_BOARD_*)"
#endif

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_PINS_H */
