/**
 * @file rccar_led.c
 * @brief 웜 화이트 + 테스트 패턴 (스텁)
 */
#include "rccar_led.h"

esp_err_t rccar_led_init(void)
{
    return ESP_OK;
}

void rccar_led_warm_white_set(bool on)
{
    (void)on;
}

void rccar_led_warm_white_toggle(void)
{
}

void rccar_led_test_chase_step(void)
{
}
