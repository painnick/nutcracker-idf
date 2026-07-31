/**
 * @file rccar_servo.h
 * @brief 레이더 CR 서보 (스텁)
 */
#ifndef RCCAR_SERVO_H
#define RCCAR_SERVO_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_servo_init(void);
void rccar_radar_set_armed(bool armed);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_SERVO_H */
