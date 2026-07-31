/**
 * @file rccar_storage.h
 * @brief NVS 저장 (볼륨 등)
 */
#ifndef RCCAR_STORAGE_H
#define RCCAR_STORAGE_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RCCAR_VOLUME_MIN     10
#define RCCAR_VOLUME_MAX     30
#define RCCAR_VOLUME_DEFAULT 20

/** NVS에서 볼륨 로드. 실패 시 기본값 사용 */
esp_err_t rccar_storage_init(void);

/** 현재 볼륨 값 (10~30) */
uint8_t rccar_storage_volume_get(void);

/** 볼륨 저장 (10~30). NVS에 기록 */
esp_err_t rccar_storage_volume_set(uint8_t vol);

/** 모든 설정 초기화 후 재부팅 */
void rccar_storage_erase_and_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_STORAGE_H */
