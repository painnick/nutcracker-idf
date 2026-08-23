/**
 * @file rccar_humidifier.c
 * @brief 가습기 모듈 GPIO (kingtiger1.1 전용, HIGH=ON)
 */
#include "rccar_humidifier.h"
#include "rccar_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rccar_humidifier";

static bool s_inited = false;
static bool s_on = false;
static esp_timer_handle_t s_restore_timer = NULL;

static void apply_gpio(bool on)
{
    /* 모듈 active-high: ON=1, OFF=0 */
    gpio_set_level(RCCAR_PIN_HUMIDIFIER, on ? 1 : 0);
}

static void restore_timer_cb(void *arg)
{
    (void)arg;
    rccar_humidifier_set(false);
}

bool rccar_humidifier_available(void)
{
#if defined(CONFIG_RCCAR_BOARD_KINGTIGER_1_1)
    return RCCAR_PIN_HUMIDIFIER != GPIO_NUM_NC;
#else
    return false;
#endif
}

esp_err_t rccar_humidifier_init(void)
{
    if (!rccar_humidifier_available()) {
        return ESP_OK;
    }
    if (s_inited) {
        return ESP_OK;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RCCAR_PIN_HUMIDIFIER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config humidifier %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_timer_create_args_t restore_args = {
        .callback = &restore_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "humidifier_restore",
    };
    ret = esp_timer_create(&restore_args, &s_restore_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create %s", esp_err_to_name(ret));
        return ret;
    }

    s_on = false;
    apply_gpio(false);
    s_inited = true;

    ESP_LOGI(TAG, "humidifier init ok (pin %d, default OFF, HIGH=ON)", (int)RCCAR_PIN_HUMIDIFIER);
    return ESP_OK;
}

void rccar_humidifier_set(bool on)
{
    if (!s_inited) {
        return;
    }
    if (s_on == on) {
        return;
    }
    s_on = on;
    apply_gpio(on);
    ESP_LOGI(TAG, "humidifier %s (pin %d, level=%d)", on ? "ON" : "OFF", (int)RCCAR_PIN_HUMIDIFIER,
             on ? 1 : 0);
}

void rccar_humidifier_pulse_on_ms(uint32_t on_ms)
{
    if (!s_inited || s_restore_timer == NULL || on_ms == 0) {
        ESP_LOGW(TAG, "pulse_on ignored (inited=%d, on_ms=%u)", (int)s_inited, (unsigned)on_ms);
        return;
    }

    esp_timer_stop(s_restore_timer);
    ESP_LOGI(TAG, "pulse ON for %u ms", (unsigned)on_ms);
    rccar_humidifier_set(true);
    esp_timer_start_once(s_restore_timer, (uint64_t)on_ms * 1000ULL);
}
