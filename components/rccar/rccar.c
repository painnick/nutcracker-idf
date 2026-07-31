/**
 * @file rccar.c
 * @brief RC Car 통합 초기화
 */
#include "rccar.h"

#include "esp_check.h"
#include "esp_log.h"

#include "rccar_motor.h"

static const char *TAG = "rccar";

esp_err_t rccar_init(void)
{
    esp_err_t ret = rccar_motor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rccar_motor_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "rccar_init ok");
    return ESP_OK;
}
