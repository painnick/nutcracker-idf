/**
 * @file rccar_radar.h
 * @brief 레이더 서보 (GPIO32, LEDC 50 Hz 14-bit)
 */
#ifndef RCCAR_RADAR_H
#define RCCAR_RADAR_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_radar_init(void);

/** @brief 휠이 움직이면 편도 종료 후 멈추고, 정지하면 5초 뒤 왕복을 재개한다. */
void rccar_radar_set_moving(bool moving);

/** @brief 레이더 왕복 기능. 기본은 OFF. ON이면 PWM을 붙이고, OFF면 뗀다. */
void rccar_radar_set_enabled(bool enabled);
void rccar_radar_toggle(void);
bool rccar_radar_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_RADAR_H */
