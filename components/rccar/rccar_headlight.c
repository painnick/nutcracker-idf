/**
 * @file rccar_headlight.c
 * @brief 헤드라이트 LED GPIO (GPIO14, HIGH=ON)
 */
#include "rccar_headlight.h"
#include "rccar_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "rccar_headlight";

static bool s_inited = false;
static bool s_on = false;

static void apply_gpio(bool on)
{
#if RCCAR_HEADLIGHT_ACTIVE_LOW
    gpio_set_level(RCCAR_PIN_HEADLIGHT, on ? 0 : 1);
#else
    gpio_set_level(RCCAR_PIN_HEADLIGHT, on ? 1 : 0);
#endif
}

esp_err_t rccar_headlight_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RCCAR_PIN_HEADLIGHT),
        .mode = GPIO_MODE_OUTPUT,
#if RCCAR_HEADLIGHT_ACTIVE_LOW
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
#else
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
#endif
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config headlight %s", esp_err_to_name(ret));
        return ret;
    }

    s_on = false;
    apply_gpio(false);
    s_inited = true;

    ESP_LOGI(TAG, "headlight init ok (pin %d, HIGH=ON)", (int)RCCAR_PIN_HEADLIGHT);
    return ESP_OK;
}

void rccar_headlight_set(bool on)
{
    if (!s_inited || s_on == on) {
        return;
    }
    s_on = on;
    apply_gpio(on);
    ESP_LOGI(TAG, "headlight %s (pin %d)", on ? "ON" : "OFF", (int)RCCAR_PIN_HEADLIGHT);
}

void rccar_headlight_toggle(void)
{
    if (!s_inited) {
        return;
    }
    rccar_headlight_set(!s_on);
}

bool rccar_headlight_is_on(void)
{
    return s_on;
}
