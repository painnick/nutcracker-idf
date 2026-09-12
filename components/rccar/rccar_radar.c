/**
 * @file rccar_radar.c
 * @brief 레이더 서보. 정지 5초 후 0°↔180° 편도 3초, 끝에서 3초 휴식. 주행 중 정지.
 */
#include "rccar_radar.h"
#include "rccar_pins.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

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
#define RADAR_ONE_WAY_MS     3000
#define RADAR_END_REST_MS    3000
#define RADAR_START_DELAY_MS 5000
#define RADAR_UPDATE_MS      20
#define RADAR_TASK_STACK     4096
#define RADAR_TASK_PRIO      3

enum {
    RADAR_PHASE_SWEEP = 0,
    RADAR_PHASE_DWELL,
};

static bool s_inited = false;
static TaskHandle_t s_task = NULL;
static volatile bool s_vehicle_moving = false;
static volatile bool s_need_start_delay = true;
static volatile int64_t s_stopped_at_ms = 0;

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

static int opposite_end(int deg)
{
    return (deg >= (RADAR_SWEEP_MIN_DEG + RADAR_SWEEP_MAX_DEG) / 2)
           ? RADAR_SWEEP_MIN_DEG
           : RADAR_SWEEP_MAX_DEG;
}

static int sweep_now_deg(int start_deg, int target_deg, int64_t t0_ms, int64_t now_ms)
{
    int span = target_deg - start_deg;
    int abs_span = span >= 0 ? span : -span;
    int64_t duration_ms = ((int64_t)abs_span * RADAR_ONE_WAY_MS) / (RADAR_SWEEP_MAX_DEG - RADAR_SWEEP_MIN_DEG);
    if (duration_ms <= 0) {
        return target_deg;
    }

    int64_t elapsed = now_ms - t0_ms;
    if (elapsed >= duration_ms) {
        return target_deg;
    }
    return start_deg + (int)((span * elapsed) / duration_ms);
}

static void begin_sweep(int *start_deg, int *target_deg, int *phase,
                        int64_t *sweep_t0_ms, int current_deg, int64_t now_ms)
{
    *start_deg = current_deg;
    if (current_deg == *target_deg) {
        *target_deg = opposite_end(current_deg);
    }
    *sweep_t0_ms = now_ms;
    *phase = RADAR_PHASE_SWEEP;
    ESP_LOGI(TAG, "sweep %d -> %d", *start_deg, *target_deg);
}

static void radar_sweep_task(void *arg)
{
    (void)arg;

    int deg = RADAR_SWEEP_MIN_DEG;
    int start_deg = RADAR_SWEEP_MIN_DEG;
    int target_deg = RADAR_SWEEP_MAX_DEG;
    int phase = RADAR_PHASE_SWEEP;
    int last_deg = -1;
    int64_t sweep_t0_ms = 0;
    int64_t dwell_t0_ms = 0;
    bool was_moving = false;

    s_stopped_at_ms = esp_timer_get_time() / 1000;
    s_need_start_delay = true;

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        bool moving = s_vehicle_moving;

        if (moving) {
            if (!was_moving) {
                ESP_LOGI(TAG, "hold at %d (vehicle moving)", deg);
                was_moving = true;
            }
        } else {
            if (was_moving) {
                was_moving = false;
            }

            if (s_need_start_delay) {
                if (now_ms - s_stopped_at_ms >= RADAR_START_DELAY_MS) {
                    s_need_start_delay = false;
                    begin_sweep(&start_deg, &target_deg, &phase, &sweep_t0_ms, deg, now_ms);
                }
            } else if (phase == RADAR_PHASE_DWELL) {
                if (now_ms - dwell_t0_ms >= RADAR_END_REST_MS) {
                    target_deg = opposite_end(deg);
                    begin_sweep(&start_deg, &target_deg, &phase, &sweep_t0_ms, deg, now_ms);
                }
            } else {
                deg = sweep_now_deg(start_deg, target_deg, sweep_t0_ms, now_ms);
                if (deg == target_deg) {
                    phase = RADAR_PHASE_DWELL;
                    dwell_t0_ms = now_ms;
                    ESP_LOGI(TAG, "dwell %d deg %d ms", deg, RADAR_END_REST_MS);
                }
            }
        }

        if (deg != last_deg) {
            if (!radar_servo_set_deg(deg)) {
                vTaskDelay(pdMS_TO_TICKS(RADAR_START_DELAY_MS));
                continue;
            }
            last_deg = deg;
        }
        vTaskDelay(pdMS_TO_TICKS(RADAR_UPDATE_MS));
    }
}

void rccar_radar_set_moving(bool moving)
{
    if (!s_inited) {
        return;
    }
    if (s_vehicle_moving == moving) {
        return;
    }

    s_vehicle_moving = moving;
    if (!moving) {
        s_stopped_at_ms = esp_timer_get_time() / 1000;
        s_need_start_delay = true;
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

    s_vehicle_moving = false;
    s_need_start_delay = true;
    s_stopped_at_ms = esp_timer_get_time() / 1000;
    s_inited = true;

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        radar_sweep_task, "radar", RADAR_TASK_STACK, NULL,
        RADAR_TASK_PRIO, &s_task, 0);
    if (task_ret != pdPASS) {
        s_inited = false;
        ESP_LOGE(TAG, "radar task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "init ok (pin %d, %d-%d deg, one-way %d ms, end rest %d ms, start delay %d ms)",
             (int)RCCAR_PIN_RADAR_SERVO,
             RADAR_SWEEP_MIN_DEG, RADAR_SWEEP_MAX_DEG,
             RADAR_ONE_WAY_MS, RADAR_END_REST_MS, RADAR_START_DELAY_MS);
    return ESP_OK;
}
