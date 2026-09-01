/**
 * @file rccar_laser.c
 * @brief 레이저 LED GPIO (GPIO15). King Tiger 포 발사 LED 타이밍(후좌 제외).
 */
#include "rccar_laser.h"
#include "rccar_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rccar_laser";

/* panzer4/king-tiger GUN_DELAY_MS, GUN_RETURN_WAIT_MS (후륜 밀림 타이밍은 제외) */
#define LASER_LED_DELAY_MS   400
#define LASER_LED_ON_MS      200

static bool s_inited = false;
static esp_timer_handle_t s_led_on_timer = NULL;
static esp_timer_handle_t s_led_off_timer = NULL;

static void apply_gpio(bool on)
{
#if RCCAR_LASER_ACTIVE_LOW
    gpio_set_level(RCCAR_PIN_LASER, on ? 0 : 1);
#else
    gpio_set_level(RCCAR_PIN_LASER, on ? 1 : 0);
#endif
}

static void led_off_timer_cb(void *arg)
{
    (void)arg;
    apply_gpio(false);
}

static void led_on_timer_cb(void *arg)
{
    (void)arg;
    apply_gpio(true);
    esp_timer_stop(s_led_off_timer);
    esp_timer_start_once(s_led_off_timer, (uint64_t)LASER_LED_ON_MS * 1000ULL);
}

esp_err_t rccar_laser_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RCCAR_PIN_LASER),
        .mode = GPIO_MODE_OUTPUT,
#if RCCAR_LASER_ACTIVE_LOW
        /* P-MOSFET 하이사이드 등: 부팅/초기화 전 게이트 HIGH(OFF) 유지 */
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
        ESP_LOGE(TAG, "gpio_config laser %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_timer_create_args_t on_args = {
        .callback = &led_on_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "laser_on",
    };
    ret = esp_timer_create(&on_args, &s_led_on_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create on %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_timer_create_args_t off_args = {
        .callback = &led_off_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "laser_off",
    };
    ret = esp_timer_create(&off_args, &s_led_off_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create off %s", esp_err_to_name(ret));
        return ret;
    }

    apply_gpio(false);
    s_inited = true;
    ESP_LOGI(TAG, "laser init ok (pin %d, active-low, delay %d ms, on %d ms)", (int)RCCAR_PIN_LASER,
             LASER_LED_DELAY_MS, LASER_LED_ON_MS);
    return ESP_OK;
}

void rccar_laser_fire(void)
{
    if (!s_inited) {
        return;
    }

    esp_timer_stop(s_led_on_timer);
    esp_timer_stop(s_led_off_timer);
    apply_gpio(false);
    esp_timer_start_once(s_led_on_timer, (uint64_t)LASER_LED_DELAY_MS * 1000ULL);
}

void rccar_laser_stop(void)
{
    if (!s_inited) {
        return;
    }
    esp_timer_stop(s_led_on_timer);
    esp_timer_stop(s_led_off_timer);
    apply_gpio(false);
}
