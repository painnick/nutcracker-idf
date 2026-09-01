/**
 * @file rccar_laser.h
 * @brief 레이저 LED GPIO (GPIO15, LOW=ON)
 */
#ifndef RCCAR_LASER_H
#define RCCAR_LASER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_laser_init(void);

/** @brief 포 발사 시퀀스 (LED 점등 후 자동 소등, 후좌 없음) */
void rccar_laser_fire(void);

/** @brief 진행 중 시퀀스 중단 및 LED 소등 */
void rccar_laser_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_LASER_H */
