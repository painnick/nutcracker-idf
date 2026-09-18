/**
 * @file rccar_laser.c
 * @brief 레이저 LED GPIO15, 개틀링 LED GPIO12.
 */
#include "rccar_laser.h"
#include "rccar_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rccar_laser";

/* LED는 발사 즉시 켜고, 효과음보다 0.2초 빠르게 맞춘다. */
#define LASER_LED_ON_MS      1200
#define GATLING_BLINK_MS     75
#define GATLING_FIRE_MS      500

static bool s_inited = false;
static bool s_gatling_on = false;
static esp_timer_handle_t s_led_off_timer = NULL;
static esp_timer_handle_t s_gatling_blink_timer = NULL;
static esp_timer_handle_t s_gatling_stop_timer = NULL;

static void apply_gpio(bool on)
{
#if RCCAR_LASER_ACTIVE_LOW
    gpio_set_level(RCCAR_PIN_LASER, on ? 0 : 1);
#else
    gpio_set_level(RCCAR_PIN_LASER, on ? 1 : 0);
#endif
}

static void apply_gatling_gpio(bool on)
{
#if RCCAR_GATLING_ACTIVE_LOW
    gpio_set_level(RCCAR_PIN_GATLING, on ? 0 : 1);
#else
    gpio_set_level(RCCAR_PIN_GATLING, on ? 1 : 0);
#endif
}

static void cancel_cannon(void)
{
    esp_timer_stop(s_led_off_timer);
}

static void cancel_gatling(void)
{
    esp_timer_stop(s_gatling_blink_timer);
    esp_timer_stop(s_gatling_stop_timer);
    s_gatling_on = false;
}

static void led_off_timer_cb(void *arg)
{
    (void)arg;
    apply_gpio(false);
}

static void gatling_blink_cb(void *arg)
{
    (void)arg;
    s_gatling_on = !s_gatling_on;
    apply_gatling_gpio(s_gatling_on);
}

static void gatling_stop_cb(void *arg)
{
    (void)arg;
    cancel_gatling();
    apply_gatling_gpio(false);
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
    /* 출력으로 바꾸기 전에 OFF 레벨을 래치한다. 리셋 후 0이면 활성-Low가 순간 ON이 된다. */
#if RCCAR_LASER_ACTIVE_LOW
    gpio_set_level(RCCAR_PIN_LASER, 1);
#else
    gpio_set_level(RCCAR_PIN_LASER, 0);
#endif
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config laser %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_config_t gatling_io = {
        .pin_bit_mask = (1ULL << RCCAR_PIN_GATLING),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(RCCAR_PIN_GATLING, 0);
    ret = gpio_config(&gatling_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config gatling %s", esp_err_to_name(ret));
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

    const esp_timer_create_args_t gatling_blink_args = {
        .callback = &gatling_blink_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gatling_blink",
    };
    ret = esp_timer_create(&gatling_blink_args, &s_gatling_blink_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create gatling blink %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_timer_create_args_t gatling_stop_args = {
        .callback = &gatling_stop_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gatling_stop",
    };
    ret = esp_timer_create(&gatling_stop_args, &s_gatling_stop_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create gatling stop %s", esp_err_to_name(ret));
        return ret;
    }

    apply_gpio(false);
    apply_gatling_gpio(false);
    s_inited = true;
    ESP_LOGI(TAG, "laser init ok (cannon gpio %d on %d ms, gatling gpio %d, gatling %d ms)",
             (int)RCCAR_PIN_LASER, LASER_LED_ON_MS, (int)RCCAR_PIN_GATLING, GATLING_FIRE_MS);
    return ESP_OK;
}

void rccar_laser_fire(void)
{
    if (!s_inited) {
        return;
    }

    cancel_cannon();
    apply_gpio(true);
    esp_timer_start_once(s_led_off_timer, (uint64_t)LASER_LED_ON_MS * 1000ULL);
}

void rccar_laser_gatling(void)
{
    if (!s_inited) {
        return;
    }

    cancel_gatling();
    s_gatling_on = true;
    apply_gatling_gpio(true);
    esp_timer_start_periodic(s_gatling_blink_timer, (uint64_t)GATLING_BLINK_MS * 1000ULL);
    esp_timer_start_once(s_gatling_stop_timer, (uint64_t)GATLING_FIRE_MS * 1000ULL);
}

void rccar_laser_stop(void)
{
    if (!s_inited) {
        return;
    }
    cancel_cannon();
    cancel_gatling();
    apply_gpio(false);
    apply_gatling_gpio(false);
}
