/**
 * @file rccar.c
 * @brief RC Car 통합 초기화
 *
 * 순서: motor → servo → shiftreg → led
 * (storage/dfplayer는 이후 태스크)
 */
#include "rccar.h"

#include "esp_log.h"

#include "rccar_led.h"
#include "rccar_motor.h"
#include "rccar_servo.h"
#include "rccar_shiftreg.h"

static const char *TAG = "rccar";

esp_err_t rccar_init(void)
{
    esp_err_t ret;

    ret = rccar_motor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rccar_motor_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rccar_servo_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rccar_servo_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rccar_shiftreg_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rccar_shiftreg_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rccar_led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rccar_led_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "rccar_init ok");
    return ESP_OK;
}
