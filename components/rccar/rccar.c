/**
 * @file rccar.c
 * @brief RC Car 통합 초기화
 *
 * 순서: storage → motor → servo → shiftreg → led
 * DFPlayer는 uni_init() 밖에서(on_init_complete) 따로 초기화한다.
 */
#include "rccar.h"

#include "esp_check.h"
#include "esp_log.h"

#include "rccar_dfplayer.h"
#include "rccar_humidifier.h"
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
    ESP_RETURN_ON_ERROR(rccar_humidifier_init(), TAG, "humidifier");

    /* DFPlayer는 여기서 초기화하지 않는다. rccar_init은 my_platform_init에서,
       즉 uni_init() 도중에 불린다. UART 드라이버 설치와 200ms 대기를 BT 스택
       초기화 한복판에서 하면 연결이 불안정해진다. panzer4/king-tiger와 같이
       on_init_complete에서 rccar_dfplayer_init()을 호출한다. */

    ESP_LOGI(TAG, "rccar_init ok (volume=%u)", (unsigned)rccar_storage_volume_get());
    return ESP_OK;
}
