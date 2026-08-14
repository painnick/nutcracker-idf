/**
 * @file rccar_motor.c
 * @brief MCPWM 4휠 + 포탑 (DRV8833 x3)
 *
 * MCPWM group 0: FL, FR, RL (operators 3)
 * MCPWM group 1: RR, TURRET (operators 2)
 * 20 kHz, resolution 1 MHz. 램프 없이 지정한 속도를 즉시 듀티에 반영한다.
 */
#include "rccar_motor.h"
#include "rccar_pins.h"

#include "driver/mcpwm_prelude.h"
#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "rccar_motor";

#define MCPWM_FREQ_HZ        (20000)
#define MCPWM_RESOLUTION_HZ  (1000000)
#define MCPWM_PERIOD_TICKS   (MCPWM_RESOLUTION_HZ / MCPWM_FREQ_HZ)

/* 스틱/믹스 범위 ±512 → 듀티 비율 */
#define AXIS_MAX             512

/* 0이 아닌 명령의 최소 듀티. 정지 마찰을 이기지 못해 웅웅거리는 것을 막는다. */
#define WHEEL_MIN_SPEED      448

enum {
    WHEEL_FL = 0,
    WHEEL_FR,
    WHEEL_RL,
    WHEEL_RR,
    WHEEL_COUNT
};

enum {
    MOTOR_FL = 0,
    MOTOR_FR,
    MOTOR_RL,
    MOTOR_RR,
    MOTOR_TURRET,
    MOTOR_COUNT
};

typedef struct {
    mcpwm_cmpr_handle_t cmpr_a;
    mcpwm_cmpr_handle_t cmpr_b;
} motor_channel_t;

static motor_channel_t s_motors[MOTOR_COUNT];
static mcpwm_timer_handle_t s_timer0 = NULL;
static mcpwm_timer_handle_t s_timer1 = NULL;

static SemaphoreHandle_t s_motor_mutex = NULL;
static bool s_inited = false;

static void set_motor_duty(mcpwm_cmpr_handle_t cmpr_a, mcpwm_cmpr_handle_t cmpr_b, int32_t speed)
{
    uint32_t ticks;
    if (speed > 0) {
        ticks = ((uint32_t)speed * MCPWM_PERIOD_TICKS) / AXIS_MAX;
        if (ticks > MCPWM_PERIOD_TICKS) {
            ticks = MCPWM_PERIOD_TICKS;
        }
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_a, ticks));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_b, 0));
    } else if (speed < 0) {
        ticks = ((uint32_t)(-speed) * MCPWM_PERIOD_TICKS) / AXIS_MAX;
        if (ticks > MCPWM_PERIOD_TICKS) {
            ticks = MCPWM_PERIOD_TICKS;
        }
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_a, 0));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_b, ticks));
    } else {
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_a, 0));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_b, 0));
    }
}

/* 0은 그대로, 그 외에는 최소 듀티 이상으로 올린다 (부호 유지) */
static int32_t apply_min_speed(int32_t v)
{
    if (v == 0) {
        return 0;
    }
    if (v > 0) {
        return (v < WHEEL_MIN_SPEED) ? WHEEL_MIN_SPEED : v;
    }
    return (v > -WHEEL_MIN_SPEED) ? -WHEEL_MIN_SPEED : v;
}

static esp_err_t setup_generator_pair(mcpwm_oper_handle_t oper,
                                      mcpwm_cmpr_handle_t cmpr_a,
                                      mcpwm_cmpr_handle_t cmpr_b,
                                      int gpio_in1,
                                      int gpio_in2)
{
    mcpwm_generator_config_t gen_config = {
        .gen_gpio_num = gpio_in1,
    };
    mcpwm_gen_handle_t gen_a = NULL;
    mcpwm_gen_handle_t gen_b = NULL;
    esp_err_t ret;

    ret = mcpwm_new_generator(oper, &gen_config, &gen_a);
    ESP_RETURN_ON_ERROR(ret, TAG, "gen_a gpio %d", gpio_in1);

    gen_config.gen_gpio_num = gpio_in2;
    ret = mcpwm_new_generator(oper, &gen_config, &gen_b);
    ESP_RETURN_ON_ERROR(ret, TAG, "gen_b gpio %d", gpio_in2);

    ret = mcpwm_generator_set_actions_on_timer_event(
        gen_a,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
        MCPWM_GEN_TIMER_EVENT_ACTION_END());
    ESP_RETURN_ON_ERROR(ret, TAG, "gen_a timer action");
    ret = mcpwm_generator_set_actions_on_compare_event(
        gen_a,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpr_a, MCPWM_GEN_ACTION_LOW),
        MCPWM_GEN_COMPARE_EVENT_ACTION_END());
    ESP_RETURN_ON_ERROR(ret, TAG, "gen_a compare action");

    ret = mcpwm_generator_set_actions_on_timer_event(
        gen_b,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
        MCPWM_GEN_TIMER_EVENT_ACTION_END());
    ESP_RETURN_ON_ERROR(ret, TAG, "gen_b timer action");
    ret = mcpwm_generator_set_actions_on_compare_event(
        gen_b,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpr_b, MCPWM_GEN_ACTION_LOW),
        MCPWM_GEN_COMPARE_EVENT_ACTION_END());
    ESP_RETURN_ON_ERROR(ret, TAG, "gen_b compare action");

    return ESP_OK;
}

static esp_err_t setup_motor_on_group(int group_id,
                                      mcpwm_timer_handle_t timer,
                                      int motor_idx,
                                      int gpio_in1,
                                      int gpio_in2)
{
    esp_err_t ret;
    mcpwm_oper_handle_t oper = NULL;
    mcpwm_operator_config_t op_config = {
        .group_id = group_id,
    };

    ret = mcpwm_new_operator(&op_config, &oper);
    ESP_RETURN_ON_ERROR(ret, TAG, "operator m%d g%d", motor_idx, group_id);

    ret = mcpwm_operator_connect_timer(oper, timer);
    ESP_RETURN_ON_ERROR(ret, TAG, "connect timer m%d", motor_idx);

    mcpwm_comparator_config_t cmpr_config = {
        .flags.update_cmp_on_tez = true,
    };

    ret = mcpwm_new_comparator(oper, &cmpr_config, &s_motors[motor_idx].cmpr_a);
    ESP_RETURN_ON_ERROR(ret, TAG, "cmpr_a m%d", motor_idx);
    ret = mcpwm_new_comparator(oper, &cmpr_config, &s_motors[motor_idx].cmpr_b);
    ESP_RETURN_ON_ERROR(ret, TAG, "cmpr_b m%d", motor_idx);

    ret = setup_generator_pair(oper,
                               s_motors[motor_idx].cmpr_a,
                               s_motors[motor_idx].cmpr_b,
                               gpio_in1,
                               gpio_in2);
    ESP_RETURN_ON_ERROR(ret, TAG, "generators m%d", motor_idx);

    return ESP_OK;
}

esp_err_t rccar_motor_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    esp_err_t ret;

    for (int i = 0; i < MOTOR_COUNT; i++) {
        s_motors[i].cmpr_a = NULL;
        s_motors[i].cmpr_b = NULL;
    }

    s_motor_mutex = xSemaphoreCreateMutex();
    if (!s_motor_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_FAIL;
    }

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_RESOLUTION_HZ,
        .period_ticks = MCPWM_PERIOD_TICKS,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };

    ret = mcpwm_new_timer(&timer_config, &s_timer0);
    ESP_RETURN_ON_ERROR(ret, TAG, "timer group0");

    timer_config.group_id = 1;
    ret = mcpwm_new_timer(&timer_config, &s_timer1);
    ESP_RETURN_ON_ERROR(ret, TAG, "timer group1");

    /* group 0: FL, FR, RL */
    ret = setup_motor_on_group(0, s_timer0, MOTOR_FL, RCCAR_PIN_FL_IN1, RCCAR_PIN_FL_IN2);
    ESP_RETURN_ON_ERROR(ret, TAG, "FL");
    ret = setup_motor_on_group(0, s_timer0, MOTOR_FR, RCCAR_PIN_FR_IN1, RCCAR_PIN_FR_IN2);
    ESP_RETURN_ON_ERROR(ret, TAG, "FR");
    ret = setup_motor_on_group(0, s_timer0, MOTOR_RL, RCCAR_PIN_RL_IN1, RCCAR_PIN_RL_IN2);
    ESP_RETURN_ON_ERROR(ret, TAG, "RL");

    /* group 1: RR, TURRET (TURRET 핀이 NC면 미장착으로 보고 건너뜀) */
    ret = setup_motor_on_group(1, s_timer1, MOTOR_RR, RCCAR_PIN_RR_IN1, RCCAR_PIN_RR_IN2);
    ESP_RETURN_ON_ERROR(ret, TAG, "RR");
    if (RCCAR_PIN_TURRET_IN1 != GPIO_NUM_NC && RCCAR_PIN_TURRET_IN2 != GPIO_NUM_NC) {
        ret = setup_motor_on_group(1, s_timer1, MOTOR_TURRET, RCCAR_PIN_TURRET_IN1, RCCAR_PIN_TURRET_IN2);
        ESP_RETURN_ON_ERROR(ret, TAG, "TURRET");
    } else {
        ESP_LOGI(TAG, "turret disabled (pins NC)");
    }

    ret = mcpwm_timer_enable(s_timer0);
    ESP_RETURN_ON_ERROR(ret, TAG, "timer0 enable");
    ret = mcpwm_timer_start_stop(s_timer0, MCPWM_TIMER_START_NO_STOP);
    ESP_RETURN_ON_ERROR(ret, TAG, "timer0 start");

    ret = mcpwm_timer_enable(s_timer1);
    ESP_RETURN_ON_ERROR(ret, TAG, "timer1 enable");
    ret = mcpwm_timer_start_stop(s_timer1, MCPWM_TIMER_START_NO_STOP);
    ESP_RETURN_ON_ERROR(ret, TAG, "timer1 start");

    s_inited = true;

    rccar_motor_wheel_set(0, 0, 0, 0);
    rccar_motor_turret_set(0);

    ESP_LOGI(TAG, "motor init ok (g0:FL/FR/RL g1:RR/TURRET, %d Hz)", MCPWM_FREQ_HZ);
    return ESP_OK;
}

void rccar_motor_wheel_set(int fl, int fr, int rl, int rr)
{
    if (!s_inited || !s_motor_mutex) {
        return;
    }
    if (xSemaphoreTake(s_motor_mutex, portMAX_DELAY) == pdTRUE) {
        const int vals[WHEEL_COUNT] = { fl, fr, rl, rr };
        for (int i = 0; i < WHEEL_COUNT; i++) {
            if (s_motors[i].cmpr_a && s_motors[i].cmpr_b) {
                set_motor_duty(s_motors[i].cmpr_a, s_motors[i].cmpr_b, apply_min_speed(vals[i]));
            }
        }
        xSemaphoreGive(s_motor_mutex);
    }
}

void rccar_motor_turret_set(int speed)
{
    if (!s_inited) {
        return;
    }
    if (s_motors[MOTOR_TURRET].cmpr_a && s_motors[MOTOR_TURRET].cmpr_b) {
        set_motor_duty(s_motors[MOTOR_TURRET].cmpr_a, s_motors[MOTOR_TURRET].cmpr_b, speed);
    }
}

void rccar_motor_all_stop(void)
{
    rccar_motor_wheel_set(0, 0, 0, 0);
    rccar_motor_turret_set(0);
}
