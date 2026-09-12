/**
 * @file rccar_radar.c
 * @brief 레이더 서보. 정지 5초 후 0°↔180° 편도 3초, 끝에서 1초 후 PWM 해제.
 *        끝에서 3초 휴식. 주행이 시작돼도 진행 중인 편도는 끝까지 간다.
 */
#include "rccar_radar.h"
#include "rccar_pins.h"

#include "driver/gpio.h"
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
#define RADAR_DETACH_MS      1000
#define RADAR_REATTACH_HOLD_MS 60
#define RADAR_START_DELAY_MS 5000
#define RADAR_UPDATE_MS      20
#define RADAR_TASK_STACK     4096
#define RADAR_TASK_PRIO      3

enum {
    RADAR_PHASE_IDLE = 0,
    RADAR_PHASE_SWEEP,
    RADAR_PHASE_DWELL,
};

static bool s_inited = false;
static bool s_channel_ready = false;
static bool s_attached = false;
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

static bool radar_servo_apply_duty(int degree)
{
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

static bool radar_servo_attach(int degree)
{
    if (s_attached) {
        return true;
    }

    uint32_t duty = degree_to_duty(degree);
    if (!s_channel_ready) {
        ledc_channel_config_t ch_config = {
            .gpio_num = RCCAR_PIN_RADAR_SERVO,
            .speed_mode = LEDC_MODE,
            .channel = LEDC_CHANNEL,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER,
            .duty = duty,
            .hpoint = 0,
        };
        esp_err_t ret = ledc_channel_config(&ch_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "attach failed: %s", esp_err_to_name(ret));
            return false;
        }
        s_channel_ready = true;
    } else {
        /* ledc_stop idle LOW는 쓰지 않는다. 핀을 떼기 전에 0으로 떨어지면 180°에서 0°로 튄다. */
        if (!radar_servo_apply_duty(degree)) {
            return false;
        }
        esp_err_t ret = ledc_set_pin((int)RCCAR_PIN_RADAR_SERVO, LEDC_MODE, LEDC_CHANNEL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "attach pin failed: %s", esp_err_to_name(ret));
            return false;
        }
    }

    s_attached = true;
    ESP_LOGI(TAG, "servo attached at %d deg (duty %u)", degree, (unsigned)duty);
    return true;
}

static void radar_servo_detach(void)
{
    if (!s_attached) {
        return;
    }

    /* LEDC 출력을 핀에서 먼저 분리한다. ledc_stop(..., 0)은 핀이 붙은 채 LOW가 된다. */
    gpio_reset_pin(RCCAR_PIN_RADAR_SERVO);
    gpio_set_direction(RCCAR_PIN_RADAR_SERVO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(RCCAR_PIN_RADAR_SERVO, GPIO_FLOATING);
    s_attached = false;
    ESP_LOGI(TAG, "servo detached");
}

static bool radar_servo_set_deg(int degree)
{
    if (!s_inited || !radar_servo_attach(degree)) {
        return false;
    }
    return radar_servo_apply_duty(degree);
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
                        int64_t *sweep_t0_ms, int *last_deg, int current_deg)
{
    *start_deg = current_deg;
    if (current_deg == *target_deg) {
        *target_deg = opposite_end(current_deg);
    }
    if (!radar_servo_set_deg(current_deg)) {
        *phase = RADAR_PHASE_IDLE;
        return;
    }
    *last_deg = current_deg;
    /* 50 Hz 몇 주기 동안 시작 각도를 유지한 뒤 왕복을 시작한다. */
    vTaskDelay(pdMS_TO_TICKS(RADAR_REATTACH_HOLD_MS));
    *sweep_t0_ms = esp_timer_get_time() / 1000;
    *phase = RADAR_PHASE_SWEEP;
    ESP_LOGI(TAG, "sweep %d -> %d", *start_deg, *target_deg);
}

static bool start_delay_done(int64_t now_ms)
{
    if (!s_need_start_delay) {
        return true;
    }
    if (s_vehicle_moving) {
        return false;
    }
    if (now_ms - s_stopped_at_ms >= RADAR_START_DELAY_MS) {
        s_need_start_delay = false;
        return true;
    }
    return false;
}

static void radar_sweep_task(void *arg)
{
    (void)arg;

    int deg = RADAR_SWEEP_MIN_DEG;
    int start_deg = RADAR_SWEEP_MIN_DEG;
    int target_deg = RADAR_SWEEP_MAX_DEG;
    int phase = RADAR_PHASE_IDLE;
    int last_deg = -1;
    int64_t sweep_t0_ms = 0;
    int64_t dwell_t0_ms = 0;

    s_stopped_at_ms = esp_timer_get_time() / 1000;
    s_need_start_delay = true;
    radar_servo_detach();

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (phase == RADAR_PHASE_SWEEP) {
            deg = sweep_now_deg(start_deg, target_deg, sweep_t0_ms, now_ms);
            if (deg != last_deg) {
                if (!radar_servo_set_deg(deg)) {
                    vTaskDelay(pdMS_TO_TICKS(RADAR_START_DELAY_MS));
                    continue;
                }
                last_deg = deg;
            }
            if (deg == target_deg) {
                phase = RADAR_PHASE_DWELL;
                dwell_t0_ms = now_ms;
                ESP_LOGI(TAG, "arrived %d deg", deg);
            }
        } else if (phase == RADAR_PHASE_DWELL) {
            if (s_attached && (now_ms - dwell_t0_ms >= RADAR_DETACH_MS)) {
                radar_servo_detach();
                last_deg = -1;
            }
            if (!s_vehicle_moving &&
                (now_ms - dwell_t0_ms >= RADAR_END_REST_MS) &&
                start_delay_done(now_ms)) {
                target_deg = opposite_end(deg);
                begin_sweep(&start_deg, &target_deg, &phase, &sweep_t0_ms, &last_deg, deg);
            }
        } else {
            if (s_attached) {
                radar_servo_detach();
                last_deg = -1;
            }
            if (start_delay_done(now_ms)) {
                begin_sweep(&start_deg, &target_deg, &phase, &sweep_t0_ms, &last_deg, deg);
            }
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

    s_channel_ready = false;
    s_attached = false;
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

    ESP_LOGI(TAG, "init ok (pin %d, %d-%d deg, one-way %d ms, detach %d ms, end rest %d ms, start delay %d ms)",
             (int)RCCAR_PIN_RADAR_SERVO,
             RADAR_SWEEP_MIN_DEG, RADAR_SWEEP_MAX_DEG,
             RADAR_ONE_WAY_MS, RADAR_DETACH_MS, RADAR_END_REST_MS, RADAR_START_DELAY_MS);
    return ESP_OK;
}
