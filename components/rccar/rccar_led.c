/**
 * @file rccar_led.c
 * @brief 웜 화이트 MOSFET GPIO + 595 테스트 체이스
 */
#include "rccar_led.h"
#include "rccar_pins.h"
#include "rccar_shiftreg.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "rccar_led";

static bool s_inited = false;
static bool s_warm_white_on = false;
static int s_chase_pos = 0; /* 0..31 */

esp_err_t rccar_led_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RCCAR_PIN_WARM_WHITE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config warm_white %s", esp_err_to_name(ret));
        return ret;
    }

    s_warm_white_on = false;
    gpio_set_level(RCCAR_PIN_WARM_WHITE, 0);
    s_chase_pos = 0;
    s_inited = true;

    ESP_LOGI(TAG, "led init ok (warm_white pin %d)", (int)RCCAR_PIN_WARM_WHITE);
    return ESP_OK;
}

void rccar_led_warm_white_set(bool on)
{
    if (!s_inited) {
        return;
    }
    s_warm_white_on = on;
    gpio_set_level(RCCAR_PIN_WARM_WHITE, on ? 1 : 0);
}

void rccar_led_warm_white_toggle(void)
{
    rccar_led_warm_white_set(!s_warm_white_on);
}

void rccar_led_test_chase_step(void)
{
    if (!s_inited) {
        return;
    }
    uint32_t bits = (1u << s_chase_pos);
    rccar_shiftreg_write32(bits);
    s_chase_pos = (s_chase_pos + 1) & 31;
}
