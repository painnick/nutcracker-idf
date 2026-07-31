/**
 * @file rccar_storage.h
 * @brief 볼륨 NVS (스텁)
 */
#ifndef RCCAR_STORAGE_H
#define RCCAR_STORAGE_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_storage_init(void);
uint8_t rccar_storage_volume_get(void);
esp_err_t rccar_storage_volume_set(uint8_t vol);
void rccar_storage_erase_and_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_STORAGE_H */
