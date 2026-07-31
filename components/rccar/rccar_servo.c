/**
 * @file rccar_servo.c
 * @brief 레이더 연속 회전(CR) 서보 — LEDC 50 Hz
 *
 * 정지: 중립 펄스 (~1500 us)
 * 무장: 고정 속도 회전 펄스 (RCCAR_RADAR_SPIN_US, 실기 보정)
 */
#include "rccar_servo.h"
#include "rccar_pins.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "rccar_servo";

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT
#define LEDC_FREQ_HZ    50

/* CR 서보 펄스 (us). 실기에서 방향/속도 보정 */
#ifndef RCCAR_RADAR_STOP_US
#define RCCAR_RADAR_STOP_US  1500
#endif
#ifndef RCCAR_RADAR_SPIN_US
#define RCCAR_RADAR_SPIN_US  1700
#endif

static bool s_inited = false;
static bool s_armed = false;

static uint32_t us_to_duty(uint32_t us)
{
    uint32_t max_duty = (1u << LEDC_DUTY_RES) - 1u;
    return (us * (uint32_t)LEDC_FREQ_HZ * max_duty) / 1000000u;
}

static void set_pulse_us(uint32_t us)
{
    uint32_t duty = us_to_duty(us);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

esp_err_t rccar_servo_init(void)
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
        .duty = us_to_duty(RCCAR_RADAR_STOP_US),
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ch_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config %s", esp_err_to_name(ret));
        return ret;
    }

    s_armed = false;
    set_pulse_us(RCCAR_RADAR_STOP_US);
    s_inited = true;

    ESP_LOGI(TAG, "radar servo init ok (pin %d, stop=%uus spin=%uus)",
             (int)RCCAR_PIN_RADAR_SERVO, (unsigned)RCCAR_RADAR_STOP_US,
             (unsigned)RCCAR_RADAR_SPIN_US);
    return ESP_OK;
}

void rccar_radar_set_armed(bool armed)
{
    if (!s_inited) {
        return;
    }
    s_armed = armed;
    set_pulse_us(armed ? RCCAR_RADAR_SPIN_US : RCCAR_RADAR_STOP_US);
}
