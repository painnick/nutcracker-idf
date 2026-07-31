/**
 * @file rccar_dfplayer.c
 * @brief DFPlayer 사운드 (스텁)
 */
#include "rccar_dfplayer.h"

esp_err_t rccar_dfplayer_init(void)
{
    return ESP_OK;
}

esp_err_t rccar_dfplayer_play(uint8_t track)
{
    (void)track;
    return ESP_OK;
}

esp_err_t rccar_dfplayer_set_volume(uint8_t vol)
{
    (void)vol;
    return ESP_OK;
}

esp_err_t rccar_dfplayer_stop(void)
{
    return ESP_OK;
}
