/**
 * @file rccar_pins.h
 * @brief nutcracker PCB 핀 정의
 */
#ifndef RCCAR_PINS_H
#define RCCAR_PINS_H

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DRV8833 #1 전륜 좌 */
#define RCCAR_PIN_FL_IN1 (GPIO_NUM_22)
#define RCCAR_PIN_FL_IN2 (GPIO_NUM_21)

/* DRV8833 #1 전륜 우 */
#define RCCAR_PIN_FR_IN1 (GPIO_NUM_18)
#define RCCAR_PIN_FR_IN2 (GPIO_NUM_19)

/* DRV8833 #2 후륜 좌 */
#define RCCAR_PIN_RL_IN1 (GPIO_NUM_25)
#define RCCAR_PIN_RL_IN2 (GPIO_NUM_33)

/* DRV8833 #2 후륜 우 */
#define RCCAR_PIN_RR_IN1 (GPIO_NUM_26)
#define RCCAR_PIN_RR_IN2 (GPIO_NUM_27)

/* DRV8833 #3 포탑 */
#define RCCAR_PIN_TURRET_IN1 (GPIO_NUM_16)
#define RCCAR_PIN_TURRET_IN2 (GPIO_NUM_17)

/* 가습기 MOSFET/릴레이 (HIGH=ON) */
#define RCCAR_PIN_HUMIDIFIER (GPIO_NUM_4)

/* DFPlayer Mini UART TX */
#define RCCAR_PIN_SOUND_TX (GPIO_NUM_5)
#define RCCAR_PIN_SOUND_RX (GPIO_NUM_NC)

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_PINS_H */
