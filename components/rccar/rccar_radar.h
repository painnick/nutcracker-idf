/**
 * @file rccar_radar.h
 * @brief 레이더 서보 (GPIO32, LEDC 50 Hz)
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

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_RADAR_H */
