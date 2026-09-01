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

/* WS2812 네오픽셀 (4개, DATA) */
#define RCCAR_PIN_NEOPIXEL (GPIO_NUM_13)

/* 레이저 LED MOSFET (LOW=ON, HIGH=OFF) */
#define RCCAR_PIN_LASER (GPIO_NUM_15)
#define RCCAR_LASER_ACTIVE_LOW 1

/* DFPlayer Mini UART TX */
#define RCCAR_PIN_SOUND_TX (GPIO_NUM_5)
#define RCCAR_PIN_SOUND_RX (GPIO_NUM_NC)

/* 헤드라이트 LED (HIGH=ON) */
#define RCCAR_PIN_HEADLIGHT (GPIO_NUM_14)
#define RCCAR_HEADLIGHT_ACTIVE_LOW 0

/* 레이더 서보 PWM (50 Hz) */
#define RCCAR_PIN_RADAR_SERVO (GPIO_NUM_32)

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_PINS_H */
