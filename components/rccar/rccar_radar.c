/**
 * @file rccar_radar.c
 * @brief 레이더 서보 왕복 (0°↔180°, 왕복 6초)
 */
#include "rccar_radar.h"
#include "rccar_pins.h"

#include "driver/ledc.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rccar_radar";

#define LEDC_TIMER           LEDC_TIMER_0
#define LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL         LEDC_CHANNEL_0
#define LEDC_DUTY_RES        LEDC_TIMER_10_BIT
#define LEDC_FREQ_HZ         50

#define SERVO_PULSE_MIN_US   500
#define SERVO_PULSE_MAX_US   2500
#define SERVO_DEGREE_RANGE   180

#define RADAR_SWEEP_MIN_DEG  0
#define RADAR_SWEEP_MAX_DEG  180
#define RADAR_ROUNDTRIP_MS   6000
#define RADAR_UPDATE_MS      20
#define RADAR_TASK_STACK     4096
#define RADAR_TASK_PRIO      3

static bool s_inited = false;
static TaskHandle_t s_task = NULL;

static uint32_t degree_to_duty(int degree)
{
    if (degree < 0) {
        degree = 0;
    }
    if (degree > SERVO_DEGREE_RANGE) {
        degree = SERVO_DEGREE_RANGE;
    }

    uint32_t us = SERVO_PULSE_MIN_US +
                  (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) * (uint32_t)degree / SERVO_DEGREE_RANGE;
    uint32_t max_duty = (1U << LEDC_DUTY_RES) - 1;
    return (us * LEDC_FREQ_HZ * max_duty) / 1000000U;
}

static bool radar_servo_set_deg(int degree)
{
    if (!s_inited) {
        return false;
    }

    uint32_t duty = degree_to_duty(degree);
    esp_err_t ret = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_duty failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "update_duty failed: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

static int sweep_degree(uint32_t elapsed_ms)
{
    uint32_t t = elapsed_ms % RADAR_ROUNDTRIP_MS;
    uint32_t half_ms = RADAR_ROUNDTRIP_MS / 2;
    int span = RADAR_SWEEP_MAX_DEG - RADAR_SWEEP_MIN_DEG;

    if (t < half_ms) {
        return RADAR_SWEEP_MIN_DEG + (int)(t * (uint32_t)span / half_ms);
    }
    return RADAR_SWEEP_MAX_DEG - (int)((t - half_ms) * (uint32_t)span / half_ms);
}

static void radar_sweep_task(void *arg)
{
    (void)arg;

    TickType_t start = xTaskGetTickCount();
    int last_deg = -1;

    while (1) {
        uint32_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - start);
        int deg = sweep_degree(elapsed_ms);
        if (deg != last_deg) {
            if (!radar_servo_set_deg(deg)) {
                vTaskDelay(pdMS_TO_TICKS(RADAR_ROUNDTRIP_MS));
                continue;
            }
            last_deg = deg;
        }
        vTaskDelay(pdMS_TO_TICKS(RADAR_UPDATE_MS));
    }
}

esp_err_t rccar_radar_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t ch_config = {
        .gpio_num = RCCAR_PIN_RADAR_SERVO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = degree_to_duty(RADAR_SWEEP_MIN_DEG),
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ch_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config pin %d: %s",
                 (int)RCCAR_PIN_RADAR_SERVO, esp_err_to_name(ret));
        return ret;
    }

    s_inited = true;

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        radar_sweep_task, "radar", RADAR_TASK_STACK, NULL,
        RADAR_TASK_PRIO, &s_task, 0);
    if (task_ret != pdPASS) {
        s_inited = false;
        ESP_LOGE(TAG, "radar task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "init ok (pin %d, %d-%d deg roundtrip %d ms)",
             (int)RCCAR_PIN_RADAR_SERVO,
             RADAR_SWEEP_MIN_DEG, RADAR_SWEEP_MAX_DEG,
             RADAR_ROUNDTRIP_MS);
    return ESP_OK;
}
