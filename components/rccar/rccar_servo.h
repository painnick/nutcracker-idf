/**
 * @file rccar_servo.h
 * @brief 레이더 CR 서보 (LEDC 50 Hz)
 */
#ifndef RCCAR_SERVO_H
#define RCCAR_SERVO_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 레이더 서보 LEDC 초기화. 정지 펄스로 시작.
 * @return ESP_OK on success
 */
esp_err_t rccar_servo_init(void);

/**
 * @brief 레이더 무장/해제
 * @param armed true: 회전 펄스(RCCAR_RADAR_SPIN_US), false: 정지(RCCAR_RADAR_STOP_US)
 */
void rccar_radar_set_armed(bool armed);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_SERVO_H */
