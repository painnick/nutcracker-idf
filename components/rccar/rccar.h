/**
 * @file rccar.h
 * @brief RC Car 하드웨어 통합 초기화
 */
#ifndef RCCAR_H
#define RCCAR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief rccar 하위 모듈 통합 초기화
 * @return ESP_OK on success
 */
esp_err_t rccar_init(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_H */
