/**
 * @file rccar.c
 * @brief RC Car 통합 초기화
 *
 * 순서: storage → motor → servo → shiftreg → led → dfplayer(+volume)
 */
#include "rccar.h"

#include "esp_check.h"
#include "esp_log.h"

#include "rccar_dfplayer.h"
#include "rccar_led.h"
#include "rccar_motor.h"
#include "rccar_servo.h"
#include "rccar_shiftreg.h"
#include "rccar_storage.h"

static const char *TAG = "rccar";

esp_err_t rccar_init(void)
{
    esp_err_t ret;

    ret = rccar_storage_init();
    if (ret != ESP_OK) {
        /* NVS 실패 시 기본 볼륨으로 계속 (panzer4와 동일) */
        ESP_LOGW(TAG, "storage init: %s, use defaults", esp_err_to_name(ret));
    }

    ESP_RETURN_ON_ERROR(rccar_motor_init(), TAG, "motor");
    ESP_RETURN_ON_ERROR(rccar_servo_init(), TAG, "servo");
    ESP_RETURN_ON_ERROR(rccar_shiftreg_init(), TAG, "shiftreg");
    ESP_RETURN_ON_ERROR(rccar_led_init(), TAG, "led");
    ESP_RETURN_ON_ERROR(rccar_dfplayer_init(), TAG, "dfplayer");

    ret = rccar_dfplayer_set_volume(rccar_storage_volume_get());
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "dfplayer set_volume: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "rccar_init ok (volume=%u)", (unsigned)rccar_storage_volume_get());
    return ESP_OK;
}
