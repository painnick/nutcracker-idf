/**
 * @file rccar_neopixel.c
 * @brief WS2812 네오픽셀 4개, 파란색 엔진 idle 떨림 효과
 */
#include "rccar_neopixel.h"
#include "rccar_pins.h"
#include "rccar_ws2812_encoder.h"

#include <string.h>

#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "rccar_neopixel";

#define RMT_RESOLUTION_HZ 10000000
#define NEOPIXEL_COUNT    4
#define ENGINE_FRAME_MS   45

static rmt_channel_handle_t s_rmt = NULL;
static rmt_encoder_handle_t s_encoder = NULL;
static esp_timer_handle_t s_anim_timer = NULL;
static SemaphoreHandle_t s_lock = NULL;
static bool s_inited = false;
static bool s_enabled = false;
static uint8_t s_pixels[NEOPIXEL_COUNT * 3];

static esp_err_t flush_pixels(const uint8_t *pixels, size_t len)
{
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    esp_err_t ret = rmt_transmit(s_rmt, s_encoder, pixels, len, &tx_config);
    if (ret != ESP_OK) {
        return ret;
    }
    return rmt_tx_wait_all_done(s_rmt, pdMS_TO_TICKS(20));
}

static void render_engine_frame(void)
{
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        /* 저속 램프 + 고속 떨림으로 엔진 idle 느낌 */
        uint8_t base = 90 + (uint8_t)(esp_random() % 90);
        uint8_t flicker = (uint8_t)(esp_random() % 70);
        uint8_t bright = base + flicker;
        if (bright < 50) {
            bright = 50;
        }

        uint8_t g = (uint8_t)((bright * 28) / 100);
        uint8_t b = bright;
        uint8_t r = (uint8_t)(esp_random() % 18);

        s_pixels[i * 3 + 0] = g;
        s_pixels[i * 3 + 1] = b;
        s_pixels[i * 3 + 2] = r;
    }
}

static void anim_timer_cb(void *arg)
{
    (void)arg;
    if (!s_inited || !s_enabled) {
        return;
    }

    if (xSemaphoreTake(s_lock, 0) != pdTRUE) {
        return;
    }

    render_engine_frame();
    esp_err_t ret = flush_pixels(s_pixels, sizeof(s_pixels));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "flush %s", esp_err_to_name(ret));
    }
    xSemaphoreGive(s_lock);
}

static void blackout_locked(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
    esp_err_t ret = flush_pixels(s_pixels, sizeof(s_pixels));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "blackout %s", esp_err_to_name(ret));
    }
}

static void set_enabled_locked(bool enabled)
{
    if (s_enabled == enabled) {
        return;
    }
    s_enabled = enabled;

    if (enabled) {
        ESP_LOGI(TAG, "engine effect ON");
        render_engine_frame();
        flush_pixels(s_pixels, sizeof(s_pixels));
        esp_timer_start_periodic(s_anim_timer, (uint64_t)ENGINE_FRAME_MS * 1000ULL);
    } else {
        esp_timer_stop(s_anim_timer);
        blackout_locked();
        ESP_LOGI(TAG, "engine effect OFF");
    }
}

esp_err_t rccar_neopixel_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RCCAR_PIN_NEOPIXEL,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_config, &s_rmt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel %s", esp_err_to_name(ret));
        return ret;
    }

    rccar_ws2812_encoder_config_t enc_config = {
        .resolution = RMT_RESOLUTION_HZ,
    };
    ret = rccar_ws2812_new_encoder(&enc_config, &s_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ws2812 encoder %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_enable(s_rmt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_timer_create_args_t anim_args = {
        .callback = &anim_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "neopixel_engine",
    };
    ret = esp_timer_create(&anim_args, &s_anim_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create %s", esp_err_to_name(ret));
        return ret;
    }

    blackout_locked();
    s_inited = true;
    ESP_LOGI(TAG, "init ok (pin %d, count %d)", (int)RCCAR_PIN_NEOPIXEL, NEOPIXEL_COUNT);
    return ESP_OK;
}

void rccar_neopixel_toggle(void)
{
    if (!s_inited || s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    set_enabled_locked(!s_enabled);
    xSemaphoreGive(s_lock);
}

void rccar_neopixel_set_enabled(bool enabled)
{
    if (!s_inited || s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    set_enabled_locked(enabled);
    xSemaphoreGive(s_lock);
}

bool rccar_neopixel_is_enabled(void)
{
    return s_enabled;
}
