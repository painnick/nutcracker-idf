/**
 * @file rccar_servo.c
 * @brief 레이더 CR 서보 (스텁)
 */
#include "rccar_servo.h"

esp_err_t rccar_servo_init(void)
{
    return ESP_OK;
}

void rccar_radar_set_armed(bool armed)
{
    (void)armed;
}
