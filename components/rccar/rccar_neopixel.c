/**
 * @file rccar_neopixel.c
 * @brief WS2812 네오픽셀 8개. 엔진 idle 떨림, 가습기 미스트(주황 유지 후 흰)
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
#define NEOPIXEL_COUNT    8
#define ENGINE_FRAME_MS   45
#define MIST_HOLD_MS      1600
#define MIST_RAMP_MS      400

/* 옅은 주황 (엔진과 동일한 G/B/R 패킹에 넣을 RGB) */
#define MIST_ORANGE_R     255
#define MIST_ORANGE_G     176
#define MIST_ORANGE_B     88

static rmt_channel_handle_t s_rmt = NULL;
static rmt_encoder_handle_t s_encoder = NULL;
static esp_timer_handle_t s_anim_timer = NULL;
static SemaphoreHandle_t s_lock = NULL;
static bool s_inited = false;
static bool s_engine_wanted = false;
static bool s_mist = false;
static int64_t s_mist_start_us = 0;
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

static uint8_t engine_bright(void)
{
    uint8_t base = 90 + (uint8_t)(esp_random() % 90);
    uint8_t flicker = (uint8_t)(esp_random() % 70);
    uint8_t bright = base + flicker;
    if (bright < 50) {
        bright = 50;
    }
    return bright;
}

static void pixel_set_rgb(int i, uint8_t r, uint8_t g, uint8_t b)
{
    /* 엔진 효과와 동일한 패킹 (G, B, R) */
    s_pixels[i * 3 + 0] = g;
    s_pixels[i * 3 + 1] = b;
    s_pixels[i * 3 + 2] = r;
}

static uint8_t lerp_u8(uint8_t a, uint8_t b, uint32_t num, uint32_t den)
{
    if (den == 0 || num >= den) {
        return b;
    }
    return (uint8_t)(((uint32_t)a * (den - num) + (uint32_t)b * num) / den);
}

static void render_engine_frame(void)
{
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        uint8_t bright = engine_bright();
        uint8_t g = (uint8_t)((bright * 28) / 100);
        uint8_t b = bright;
        uint8_t r = (uint8_t)(esp_random() % 18);
        pixel_set_rgb(i, r, g, b);
    }
}

static void render_mist_frame(void)
{
    int64_t elapsed_ms = (esp_timer_get_time() - s_mist_start_us) / 1000;
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    uint32_t t;
    if (elapsed_ms <= (int64_t)MIST_HOLD_MS) {
        t = 0;
    } else {
        int64_t ramp_elapsed = elapsed_ms - (int64_t)MIST_HOLD_MS;
        t = (ramp_elapsed >= (int64_t)MIST_RAMP_MS) ? (uint32_t)MIST_RAMP_MS : (uint32_t)ramp_elapsed;
    }

    uint8_t base_r = lerp_u8(MIST_ORANGE_R, 255, t, MIST_RAMP_MS);
    uint8_t base_g = lerp_u8(MIST_ORANGE_G, 255, t, MIST_RAMP_MS);
    uint8_t base_b = lerp_u8(MIST_ORANGE_B, 255, t, MIST_RAMP_MS);

    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        uint16_t bright = engine_bright();
        uint8_t r = (uint8_t)((base_r * bright) / 220);
        uint8_t g = (uint8_t)((base_g * bright) / 220);
        uint8_t b = (uint8_t)((base_b * bright) / 220);
        pixel_set_rgb(i, r, g, b);
    }
}

static void blackout_locked(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
    esp_err_t ret = flush_pixels(s_pixels, sizeof(s_pixels));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "blackout %s", esp_err_to_name(ret));
    }
}

static void ensure_anim_locked(void)
{
    bool want = s_mist || s_engine_wanted;
    bool running = esp_timer_is_active(s_anim_timer);

    if (want && !running) {
        if (s_mist) {
            render_mist_frame();
        } else {
            render_engine_frame();
        }
        flush_pixels(s_pixels, sizeof(s_pixels));
        esp_timer_start_periodic(s_anim_timer, (uint64_t)ENGINE_FRAME_MS * 1000ULL);
    } else if (!want && running) {
        esp_timer_stop(s_anim_timer);
        blackout_locked();
    }
}

static void anim_timer_cb(void *arg)
{
    (void)arg;
    if (!s_inited) {
        return;
    }
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) {
        return;
    }

    if (s_mist) {
        render_mist_frame();
        esp_err_t ret = flush_pixels(s_pixels, sizeof(s_pixels));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "flush %s", esp_err_to_name(ret));
        }
    } else if (s_engine_wanted) {
        render_engine_frame();
        esp_err_t ret = flush_pixels(s_pixels, sizeof(s_pixels));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "flush %s", esp_err_to_name(ret));
        }
    }

    xSemaphoreGive(s_lock);
}

static void set_engine_wanted_locked(bool enabled)
{
    if (s_engine_wanted == enabled) {
        return;
    }
    s_engine_wanted = enabled;
    ESP_LOGI(TAG, "engine effect %s", enabled ? "ON" : "OFF");
    if (!s_mist) {
        ensure_anim_locked();
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
    set_engine_wanted_locked(!s_engine_wanted);
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
    set_engine_wanted_locked(enabled);
    xSemaphoreGive(s_lock);
}

bool rccar_neopixel_is_enabled(void)
{
    return s_engine_wanted;
}

void rccar_neopixel_mist_set(bool on)
{
    if (!s_inited || s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (on) {
        s_mist = true;
        s_mist_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "mist ON (orange hold %d ms, ramp %d ms)", MIST_HOLD_MS, MIST_RAMP_MS);
        ensure_anim_locked();
        /* 타이머가 이미 돌고 있으면 첫 프레임을 당장 밀어 준다 */
        render_mist_frame();
        flush_pixels(s_pixels, sizeof(s_pixels));
    } else if (s_mist) {
        s_mist = false;
        ESP_LOGI(TAG, "mist OFF");
        ensure_anim_locked();
        if (s_engine_wanted) {
            render_engine_frame();
            flush_pixels(s_pixels, sizeof(s_pixels));
        }
    }

    xSemaphoreGive(s_lock);
}
