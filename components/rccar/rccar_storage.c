/**
 * @file rccar_storage.c
 * @brief 볼륨 NVS (스텁)
 */
#include "rccar_storage.h"

esp_err_t rccar_storage_init(void)
{
    return ESP_OK;
}

uint8_t rccar_storage_volume_get(void)
{
    return 20;
}

esp_err_t rccar_storage_volume_set(uint8_t vol)
{
    (void)vol;
    return ESP_OK;
}

void rccar_storage_erase_and_restart(void)
{
}
