/**
 * @file rccar.c
 * @brief RC Car 통합 초기화
 *
 * 순서: storage → motor → humidifier → neopixel
 * DFPlayer는 uni_init() 밖에서(on_init_complete) 따로 초기화한다.
 */
#include "rccar.h"

#include "esp_check.h"
#include "esp_log.h"

#include "rccar_humidifier.h"
#include "rccar_motor.h"
#include "rccar_neopixel.h"
#include "rccar_storage.h"

static const char *TAG = "rccar";

esp_err_t rccar_init(void)
{
    esp_err_t ret;

    ret = rccar_storage_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "storage init: %s, use defaults", esp_err_to_name(ret));
    }

    ESP_RETURN_ON_ERROR(rccar_motor_init(), TAG, "motor");
    ESP_RETURN_ON_ERROR(rccar_humidifier_init(), TAG, "humidifier");
    ESP_RETURN_ON_ERROR(rccar_neopixel_init(), TAG, "neopixel");

    ESP_LOGI(TAG, "rccar_init ok (volume=%u)", (unsigned)rccar_storage_volume_get());
    return ESP_OK;
}
