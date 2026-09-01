/**
 * @file rccar_radar.h
 * @brief 레이더 서보 (GPIO32, LEDC 50 Hz)
 */
#ifndef RCCAR_RADAR_H
#define RCCAR_RADAR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_radar_init(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_RADAR_H */
