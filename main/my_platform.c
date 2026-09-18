// RC Car platform - Bluepad32 + rccar
// 입력 처리를 Core 1로 오프로딩하여 Core 0 BT 컨트롤러 부하 경감
//
// Stick sign conventions (Bluepad32 / typical HID, after deadzone):
//   Left  Y: forward/back (vx). Left  X: yaw (w), car-like turn.
//   Right X: strafe left/right (vy). Right Y: strafe forward/back (vx), w=0.
// Adjust STICK_*_SIGN below if real hardware orientation differs.

#include <string.h>

#include <btstack_run_loop.h>
#include <platform/uni_platform.h>
#include <uni.h>
#include "controller/uni_balance_board.h"
#include "controller/uni_controller.h"
#include "controller/uni_controller_type.h"
#include "controller/uni_gamepad.h"
#include "uni_common.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "rccar.h"
#include "rccar_dfplayer.h"
#include "rccar_drive.h"
#include "rccar_headlight.h"
#include "rccar_humidifier.h"
#include "rccar_laser.h"
#include "rccar_motor.h"
#include "rccar_neopixel.h"
#include "rccar_radar.h"
#include "rccar_storage.h"

#define AXIS_MAX 512
#define AXIS_DEADZONE 60
#define FAILSAFE_MS 1000
#define TURRET_SPEED 384
#define DEBOUNCE_MS 100
#define RADAR_DEBOUNCE_MS 400
#define HEADLIGHT_DEBOUNCE_MS 400
#define LASER_DEBOUNCE_MS 400
#define GATLING_DEBOUNCE_MS 400
#define GATLING_RUMBLE_DURATION_MS 300
#define GATLING_RUMBLE_WEAK 150
#define GATLING_RUMBLE_STRONG 200
#define GUN_FIRE_DELAY_MS 200
#define GUN_RUMBLE_DURATION_MS 400
#define GUN_RUMBLE_WEAK 150
#define GUN_RUMBLE_STRONG 255
#define SELECT_START_HOLD_MS 3000
#define WHEEL_TEST_HOLD_MS 3000
#define X_RUMBLE_DURATION_MS 500
#define X_RUMBLE_WEAK 255
#define X_RUMBLE_STRONG 255
#define CONNECT_RUMBLE_DURATION_MS 400
#define CONNECT_RUMBLE_WEAK 200
#define CONNECT_RUMBLE_STRONG 200
#define SWITCH_CONNECT_RUMBLE_DURATION_MS 600
#define SWITCH_CONNECT_RUMBLE_WEAK 255
#define SWITCH_CONNECT_RUMBLE_STRONG 255
#define HUMIDIFIER_PULSE_ON_MS 2000
#define IDLE_EXHAUST_STOP_MS 1000
#define IDLE_EXHAUST_PULSE_ON_MS 1000

/* Stick axis polarity: multiply raw (post-deadzone) value. */
#define STICK_VX_SIGN (-1)  /* stick Y up = forward */
#define STICK_VY_SIGN (1)   /* right stick X right = strafe right */
#define STICK_W_SIGN (1)    /* left stick X right = yaw CW */
#define STICK_RY_VX_SIGN (-1) /* right stick Y up = strafe forward */

#define INPUT_QUEUE_LEN 1
#define INPUT_TASK_STACK 4096
#define DRIVE_LOG_INTERVAL_MS 300

static const char *DRIVE_LOG_TAG = "drive_dbg";
#define INPUT_TASK_PRIO 5
#define INPUT_POLL_MS 50
/* DS4 calibration/fw feature report 교환 후에 출력 리포트를 보낸다. */
#define GAMEPAD_EFFECT_DELAY_MS 500
#define GAMEPAD_KEEPALIVE_MS 4000
/* Balance Board: CoG 민감도 (grams). 값을 낮출수록 적은 체중 이동에도 반응한다. */
#define BB_MOVE_THRESHOLD 800
#define BB_MOVE_THRESHOLD_DIAG 500
#define BB_COG_SCALE_RANGE 2200
#define BB_SMOOTH_NUM 12
#define BB_SMOOTH_DEN 100

/* 연결이 끊긴 뒤 스캔을 다시 켜기까지의 유예. inquiry는 BR/EDR 대역을 크게
   점유해서, 컨트롤러가 스스로 재연결하려는 순간에 켜면 그 절차를 방해한다.
   Xbox Wireless 계열은 링크가 살아 있어도 새 연결을 여는 특성이 있다. */
#define SCAN_RESTART_DELAY_MS 2000
#define CONNECTED_IDLE_BGM_US (30 * 1000 * 1000)

typedef struct my_platform_instance_s {
    uni_gamepad_seat_t gamepad_seat;
    uni_balance_board_state_t bb_state;
    bool ready;
} my_platform_instance_t;

typedef struct {
    int32_t axis_x;  /* left stick X → yaw */
    int32_t axis_y;  /* left stick Y → forward/back */
    int32_t axis_rx; /* right stick X → strafe left/right */
    int32_t axis_ry; /* right stick Y → strafe forward/back (w=0) */
    uint16_t dpad;
    uint16_t buttons;
    uint8_t misc_buttons;
    bool balance_board;
    uni_hid_device_t *device;
    int64_t timestamp_ms;
} input_event_t;

static void trigger_event_on_gamepad(uni_hid_device_t *d);
static void request_rumble(uni_hid_device_t *d, uint16_t duration_ms, uint8_t weak, uint8_t strong);
static my_platform_instance_t *get_my_platform_instance(uni_hid_device_t *d);

static QueueHandle_t input_queue = NULL;
static esp_timer_handle_t restart_timer = NULL;
static esp_timer_handle_t waiting_idle_timer = NULL;
static esp_timer_handle_t connected_idle_bgm_timer = NULL;
static esp_timer_handle_t connect_sound_timer = NULL;
static esp_timer_handle_t connect_sound_play_timer = NULL;
static esp_timer_handle_t gamepad_effect_timer = NULL;
static esp_timer_handle_t gamepad_keepalive_timer = NULL;
static esp_timer_handle_t scan_restart_timer = NULL;
static esp_timer_handle_t humidifier_pulse_timer = NULL;
static esp_timer_handle_t laser_rumble_timer = NULL;
/* Core0 disconnect/ready ↔ Core1 input_process_task. false until device ready. */
static volatile bool s_connected = false;
static volatile bool s_bb_ready = false;
static volatile bool s_gp_ready = false;
static volatile int s_ready_count = 0;
static uni_hid_device_t *volatile s_bb_device = NULL;
static uni_hid_device_t *volatile s_gp_device = NULL;
static volatile int32_t s_bb_axis_y = 0;
static volatile int32_t s_bb_axis_rx = 0;
static volatile int64_t s_bb_last_ms = 0;
static input_event_t s_last_gp_evt;
static volatile int64_t s_gp_last_ms = 0;

/* play_dual_rumble()은 btstack 타이머 리스트를 조작하는데 그 리스트에는 락이 없다.
   Core1에서 직접 부르면 Core0 런루프의 타이머 순회와 경쟁해 리스트가 깨진다.
   따라서 Core1은 요청만 걸고 실제 호출은 btstack 스레드에서 수행한다. */
static btstack_context_callback_registration_t rumble_request;
static btstack_context_callback_registration_t gamepad_effect_request;
static btstack_context_callback_registration_t keepalive_request;
static uni_hid_device_t *volatile rumble_device = NULL;
static uint16_t volatile rumble_duration_ms = X_RUMBLE_DURATION_MS;
static uint8_t volatile rumble_weak = X_RUMBLE_WEAK;
static uint8_t volatile rumble_strong = X_RUMBLE_STRONG;
static uni_hid_device_t *volatile gamepad_effect_device = NULL;
static uni_hid_device_t *volatile keepalive_device = NULL;
static uint16_t s_gamepad_prev_buttons = 0;
static uni_hid_device_t *volatile laser_rumble_device = NULL;
static bool s_wheels_were_moving = false;
static int64_t s_wheels_idle_since_ms = -1;

static bool device_uses_parser_keepalive(uni_hid_device_t *d) {
    return d != NULL && d->controller_type == CONTROLLER_TYPE_XBoxOneController;
}

/* BR/EDR HID는 interrupt L2CAP(cid>0)으로 출력 리포트를 보낸다. BLE HOG는 cid가 없다. */
static bool device_supports_bredr_hid_output(uni_hid_device_t *d) {
    return d != NULL && d->conn.interrupt_cid > 0;
}

static bool device_is_switch_gamepad(uni_hid_device_t *d) {
    if (d == NULL)
        return false;
    switch (d->controller_type) {
        case CONTROLLER_TYPE_SwitchProController:
        case CONTROLLER_TYPE_SwitchJoyConLeft:
        case CONTROLLER_TYPE_SwitchJoyConRight:
            return true;
        default:
            return false;
    }
}

static bool device_is_balance_board(uni_hid_device_t *d) {
    if (d == NULL)
        return false;
    if (d->controller_subtype == CONTROLLER_SUBTYPE_WII_BALANCE_BOARD)
        return true;
    return d->controller.klass == UNI_CONTROLLER_CLASS_BALANCE_BOARD;
}

static bool device_wants_connect_rumble(uni_hid_device_t *d) {
    if (!device_supports_bredr_hid_output(d))
        return false;
    if (d->report_parser.play_dual_rumble == NULL)
        return false;
    if (d->controller_subtype == CONTROLLER_SUBTYPE_WII_BALANCE_BOARD)
        return false;
    if (d->controller.klass == UNI_CONTROLLER_CLASS_BALANCE_BOARD ||
        d->controller.klass == UNI_CONTROLLER_CLASS_KEYBOARD ||
        d->controller.klass == UNI_CONTROLLER_CLASS_MOUSE)
        return false;
    return true;
}

static void handle_x_button_press(uni_hid_device_t *d) {
    if (device_supports_bredr_hid_output(d) && d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, X_RUMBLE_DURATION_MS, X_RUMBLE_WEAK, X_RUMBLE_STRONG);
    esp_timer_stop(humidifier_pulse_timer);
    esp_timer_start_once(humidifier_pulse_timer, (uint64_t)X_RUMBLE_DURATION_MS * 1000ULL);
}

static void handle_a_button_gatling(uni_hid_device_t *d) {
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_MG);
    rccar_laser_gatling();
    request_rumble(d, GATLING_RUMBLE_DURATION_MS, GATLING_RUMBLE_WEAK, GATLING_RUMBLE_STRONG);
}

static void handle_b_button_fire(uni_hid_device_t *d) {
    rccar_laser_fire();
    laser_rumble_device = d;
    esp_timer_stop(laser_rumble_timer);
    esp_timer_start_once(laser_rumble_timer, (uint64_t)GUN_FIRE_DELAY_MS * 1000ULL);
}

static void laser_rumble_cb(void *arg) {
    (void)arg;
    uni_hid_device_t *d = laser_rumble_device;
    laser_rumble_device = NULL;
    if (d != NULL) {
        rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_GUN);
        request_rumble(d, GUN_RUMBLE_DURATION_MS, GUN_RUMBLE_WEAK, GUN_RUMBLE_STRONG);
    }
}

static void request_rumble(uni_hid_device_t *d, uint16_t duration_ms, uint8_t weak, uint8_t strong) {
    rumble_device = d;
    rumble_duration_ms = duration_ms;
    rumble_weak = weak;
    rumble_strong = strong;
    btstack_run_loop_execute_on_main_thread(&rumble_request);
}

static void rumble_on_btstack_thread(void *context) {
    (void)context;
    uni_hid_device_t *d = rumble_device;
    if (device_supports_bredr_hid_output(d) && d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, rumble_duration_ms, rumble_weak, rumble_strong);
}

static void waiting_idle_cb(void *arg) {
    (void)arg;
    rccar_dfplayer_play_loop(RCCAR_DFPLAYER_TRACK_IDLE);
}

static void connected_idle_bgm_reset(void)
{
    if (connected_idle_bgm_timer == NULL) {
        return;
    }
    esp_timer_stop(connected_idle_bgm_timer);
    if (s_connected) {
        esp_timer_start_periodic(connected_idle_bgm_timer, CONNECTED_IDLE_BGM_US);
    }
}

static void connected_idle_bgm_cb(void *arg)
{
    (void)arg;
    if (!s_connected) {
        return;
    }

    static uint8_t last_track = 0;
    uint8_t span = (uint8_t)(RCCAR_DFPLAYER_TRACK_BGM_MAX - RCCAR_DFPLAYER_TRACK_BGM_MIN + 1);
    uint8_t track = (uint8_t)(RCCAR_DFPLAYER_TRACK_BGM_MIN + (esp_random() % span));
    if (span > 1 && track == last_track) {
        track = (uint8_t)(RCCAR_DFPLAYER_TRACK_BGM_MIN + ((track - RCCAR_DFPLAYER_TRACK_BGM_MIN + 1) % span));
    }
    last_track = track;
    rccar_dfplayer_play(track);
}

static bool evt_has_control_activity(const input_event_t *evt)
{
    if (evt->buttons != 0 || evt->misc_buttons != 0 || evt->dpad != 0) {
        return true;
    }
    if (rccar_drive_apply_deadzone(evt->axis_x, AXIS_DEADZONE) != 0 ||
        rccar_drive_apply_deadzone(evt->axis_y, AXIS_DEADZONE) != 0 ||
        rccar_drive_apply_deadzone(evt->axis_rx, AXIS_DEADZONE) != 0 ||
        rccar_drive_apply_deadzone(evt->axis_ry, AXIS_DEADZONE) != 0) {
        return true;
    }
    return false;
}

/* 연결 효과음. btstack 스레드(on_device_ready)에서 UART를 쓰면 링크가 끊길 수 있어
   esp_timer 태스크에서 stop/play 한다. stop 직후 바로 play하면 DFPlayer가 무시하므로
   100ms one-shot으로 나눈다. vTaskDelay는 ESP_TIMER_TASK를 막는다. */
static void connect_sound_play_cb(void *arg) {
    (void)arg;
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_CONNECT);
}

static void connect_sound_cb(void *arg) {
    (void)arg;
    rccar_dfplayer_stop();
    if (connect_sound_play_timer != NULL) {
        esp_timer_stop(connect_sound_play_timer);
        esp_timer_start_once(connect_sound_play_timer, 100 * 1000);
    }
}

/* 유예 후 스캔 재개. esp_timer 태스크에서 실행되므로 btstack 스레드에 위임하는
   _safe 변형을 쓴다 (_unsafe는 btstack 스레드 전용). */
static void scan_restart_cb(void *arg) {
    (void)arg;
    if (s_ready_count >= CONFIG_BLUEPAD32_MAX_DEVICES)
        return;
    logi("custom: restarting scan\n");
    uni_bt_start_scanning_and_autoconnect_safe();
}

static void maybe_schedule_scan(void) {
    if (s_ready_count >= CONFIG_BLUEPAD32_MAX_DEVICES)
        return;
    esp_timer_stop(scan_restart_timer);
    esp_timer_start_once(scan_restart_timer, SCAN_RESTART_DELAY_MS * 1000);
}

static void forget_ready_device(uni_hid_device_t *d) {
    if (d == s_bb_device) {
        s_bb_device = NULL;
        s_bb_ready = false;
        s_bb_axis_y = 0;
        s_bb_axis_rx = 0;
        s_bb_last_ms = 0;
    }
    if (d == s_gp_device) {
        s_gp_device = NULL;
        s_gp_ready = false;
        s_gamepad_prev_buttons = 0;
        s_gp_last_ms = 0;
        memset(&s_last_gp_evt, 0, sizeof(s_last_gp_evt));
    }
}

static void gamepad_effect_on_btstack_thread(void *context) {
    (void)context;
    uni_hid_device_t *d = gamepad_effect_device;
    if (d != NULL) {
        trigger_event_on_gamepad(d);
        if (device_wants_connect_rumble(d)) {
            uint16_t dur = CONNECT_RUMBLE_DURATION_MS;
            uint8_t weak = CONNECT_RUMBLE_WEAK;
            uint8_t strong = CONNECT_RUMBLE_STRONG;
            if (device_is_switch_gamepad(d)) {
                dur = SWITCH_CONNECT_RUMBLE_DURATION_MS;
                weak = SWITCH_CONNECT_RUMBLE_WEAK;
                strong = SWITCH_CONNECT_RUMBLE_STRONG;
            }
            d->report_parser.play_dual_rumble(d, 0, dur, weak, strong);
        } else if (device_supports_bredr_hid_output(d) && d->report_parser.play_dual_rumble != NULL &&
                   !device_uses_parser_keepalive(d)) {
            /* DS3/Android 등: claim 출력. Xbox는 파서 keep-alive가 처리한다. */
            d->report_parser.play_dual_rumble(d, 0, 0, 0, 0);
        }
    }
}

static void gamepad_keepalive_on_btstack_thread(void *context) {
    (void)context;
    uni_hid_device_t *d = keepalive_device;
    if (d == NULL || !s_connected || !device_supports_bredr_hid_output(d))
        return;
    if (d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, 0, 0, 0);
}

static void gamepad_keepalive_cb(void *arg) {
    (void)arg;
    btstack_run_loop_execute_on_main_thread(&keepalive_request);
}

static void gamepad_keepalive_start(uni_hid_device_t *d) {
    if (device_is_balance_board(d))
        return;
    if (device_uses_parser_keepalive(d) || !device_supports_bredr_hid_output(d))
        return;
    keepalive_device = d;
    esp_timer_stop(gamepad_keepalive_timer);
    esp_timer_start_periodic(gamepad_keepalive_timer, (uint64_t)GAMEPAD_KEEPALIVE_MS * 1000ULL);
}

static void gamepad_keepalive_stop(void) {
    keepalive_device = NULL;
    esp_timer_stop(gamepad_keepalive_timer);
}

static void gamepad_effect_cb(void *arg) {
    (void)arg;
    btstack_run_loop_execute_on_main_thread(&gamepad_effect_request);
}

static void delayed_restart_cb(void *arg) {
    (void)arg;
    rccar_storage_erase_and_restart();
}

static void humidifier_pulse_cb(void *arg) {
    (void)arg;
    rccar_humidifier_pulse_on_ms(HUMIDIFIER_PULSE_ON_MS);
}

static int32_t clamp_axis(int32_t v) {
    if (v > 511)
        return 511;
    if (v < -512)
        return -512;
    return v;
}

static int32_t bb_apply_move_threshold(int32_t cog, int move_threshold) {
    if (cog > move_threshold)
        return cog - move_threshold;
    if (cog < -move_threshold)
        return cog + move_threshold;
    return 0;
}

static void bb_smooth_cog(const uni_balance_board_t *bb, uni_balance_board_state_t *state) {
    state->smooth_down = mult_frac((bb->bl + bb->br) - state->smooth_down, BB_SMOOTH_NUM, BB_SMOOTH_DEN);
    state->smooth_top = mult_frac((bb->tl + bb->tr) - state->smooth_top, BB_SMOOTH_NUM, BB_SMOOTH_DEN);
    state->smooth_left = mult_frac((bb->tl + bb->bl) - state->smooth_left, BB_SMOOTH_NUM, BB_SMOOTH_DEN);
    state->smooth_right = mult_frac((bb->tr + bb->br) - state->smooth_right, BB_SMOOTH_NUM, BB_SMOOTH_DEN);
}

/* Wii Balance Board: 4코너 무게(grams) → 무게 중심 → axis_x/axis_y (게임패드와 동일 경로). */
static void balance_board_to_stick_axes(const uni_balance_board_t *bb,
                                        uni_balance_board_state_t *state,
                                        int32_t *out_x,
                                        int32_t *out_y) {
    *out_x = 0;
    *out_y = 0;

    if (bb->tl < UNI_BALANCE_BOARD_IDLE_THRESHOLD && bb->tr < UNI_BALANCE_BOARD_IDLE_THRESHOLD &&
        bb->bl < UNI_BALANCE_BOARD_IDLE_THRESHOLD && bb->br < UNI_BALANCE_BOARD_IDLE_THRESHOLD) {
        memset(state, 0, sizeof(*state));
        return;
    }

    bb_smooth_cog(bb, state);

    int32_t cog_x = state->smooth_right - state->smooth_left;
    int32_t cog_y = state->smooth_down - state->smooth_top;

    int32_t acx = (cog_x < 0) ? -cog_x : cog_x;
    int32_t acy = (cog_y < 0) ? -cog_y : cog_y;
    int32_t move_thr = BB_MOVE_THRESHOLD;
    if (acx >= BB_MOVE_THRESHOLD_DIAG && acy >= BB_MOVE_THRESHOLD_DIAG) {
        move_thr = BB_MOVE_THRESHOLD_DIAG;
    }

    cog_x = bb_apply_move_threshold(cog_x, move_thr);
    cog_y = bb_apply_move_threshold(cog_y, move_thr);

    if (cog_x != 0)
        *out_x = clamp_axis((cog_x * AXIS_MAX) / BB_COG_SCALE_RANGE);
    if (cog_y != 0)
        *out_y = clamp_axis((cog_y * AXIS_MAX) / BB_COG_SCALE_RANGE);
}

static void log_drive_mix(int32_t vx, int32_t vy, int32_t w, const rccar_wheel_speeds_t *wheels);

static void maybe_idle_exhaust(bool moving, int64_t now_ms)
{
    rccar_radar_set_moving(moving);
    if (rccar_drive_idle_to_move(&s_wheels_were_moving, &s_wheels_idle_since_ms,
                                 moving, now_ms, IDLE_EXHAUST_STOP_MS)) {
        rccar_humidifier_pulse_on_ms(IDLE_EXHAUST_PULSE_ON_MS);
    }
}

static void apply_balance_board_drive(int32_t axis_y, int32_t axis_rx, int64_t now_ms)
{
    int32_t vx = STICK_VX_SIGN * clamp_axis(axis_y);
    int32_t vy = STICK_VY_SIGN * clamp_axis(axis_rx);
    rccar_drive_snap_diagonal_wide(&vx, &vy, AXIS_DEADZONE);

    rccar_wheel_speeds_t wheels;
    rccar_drive_mix(vx, vy, 0, &wheels);
    log_drive_mix(vx, vy, 0, &wheels);
    rccar_motor_wheel_set(wheels.fl, wheels.fr, wheels.rl, wheels.rr);
    maybe_idle_exhaust(wheels.fl != 0 || wheels.fr != 0 ||
                       wheels.rl != 0 || wheels.rr != 0, now_ms);
}

static void failsafe_stop(void) {
    maybe_idle_exhaust(false, esp_timer_get_time() / 1000);
    rccar_motor_wheel_test_stop();
    rccar_motor_all_stop();
    rccar_radar_set_enabled(false);
    rccar_humidifier_set(false);
    rccar_neopixel_set_enabled(false);
    rccar_laser_stop();
}

static void log_drive_mix(int32_t vx, int32_t vy, int32_t w, const rccar_wheel_speeds_t *wheels)
{
    static int32_t last_vx, last_vy, last_w;
    static int last_fl, last_fr, last_rl, last_rr;
    static int64_t last_log_ms = 0;

    bool changed = (vx != last_vx || vy != last_vy || w != last_w ||
                    wheels->fl != last_fl || wheels->fr != last_fr ||
                    wheels->rl != last_rl || wheels->rr != last_rr);
    if (!changed) {
        return;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    bool all_stop = (vx == 0 && vy == 0 && w == 0 &&
                     wheels->fl == 0 && wheels->fr == 0 &&
                     wheels->rl == 0 && wheels->rr == 0);
    if (!all_stop && (now_ms - last_log_ms) < DRIVE_LOG_INTERVAL_MS) {
        return;
    }

    last_vx = vx;
    last_vy = vy;
    last_w = w;
    last_fl = wheels->fl;
    last_fr = wheels->fr;
    last_rl = wheels->rl;
    last_rr = wheels->rr;
    last_log_ms = now_ms;

    ESP_LOGI(DRIVE_LOG_TAG,
             "mix vx=%ld vy=%ld w=%ld -> FL=%ld FR=%ld RL=%ld RR=%ld",
             (long)vx, (long)vy, (long)w,
             (long)wheels->fl, (long)wheels->fr, (long)wheels->rl, (long)wheels->rr);
}

static void input_process_task(void *arg) {
    (void)arg;

    static int64_t last_l1_ms = 0;
    static int64_t last_r1_ms = 0;
    static int64_t last_y_ms = 0;
    static int64_t last_a_ms = 0;
    static int64_t last_select_ms = 0;
    static int64_t last_b_ms = 0;
    static int64_t select_start_pressed_at = 0;
    static bool select_start_fired = false;
    static int64_t wheel_test_pressed_at = 0;
    static bool wheel_test_fired = false;
    static uint16_t prev_buttons = 0;
    static uint8_t prev_misc_buttons = 0;
    static int64_t last_input_ms = 0;
    static bool failsafe_active = true;

    input_event_t evt;

    while (1) {
        BaseType_t got = xQueueReceive(input_queue, &evt, pdMS_TO_TICKS(INPUT_POLL_MS));
        int64_t now_ms = esp_timer_get_time() / 1000;

        /* Disconnect / not ready: never re-drive from stale queue samples */
        if (!s_connected) {
            last_input_ms = 0;
            prev_buttons = 0;
            prev_misc_buttons = 0;
            select_start_pressed_at = 0;
            select_start_fired = false;
            wheel_test_pressed_at = 0;
            wheel_test_fired = false;
            if (!failsafe_active) {
                failsafe_stop();
                failsafe_active = true;
            }
            continue;
        }

        if (got == pdTRUE) {
            last_input_ms = evt.timestamp_ms;
        }

        /* No report for FAILSAFE_MS: 모터, 휠 테스트, 레이더, 가습기, 레이저 정지 */
        if (last_input_ms == 0 || (now_ms - last_input_ms) > FAILSAFE_MS) {
            if (!failsafe_active) {
                failsafe_stop();
                failsafe_active = true;
            }
            continue;
        }

        if (got != pdTRUE)
            continue;

        /* Re-check after receive: disconnect may race with queue read */
        if (!s_connected) {
            last_input_ms = 0;
            if (!failsafe_active) {
                failsafe_stop();
                failsafe_active = true;
            }
            continue;
        }

        failsafe_active = false;

        if (evt_has_control_activity(&evt) ||
            (s_gp_ready && evt_has_control_activity(&s_last_gp_evt))) {
            connected_idle_bgm_reset();
        }

        /* 보드와 패드가 같이 있으면 버튼/포탑은 마지막 패드 리포트를 쓴다.
           큐 길이 1이라 보드 리포트가 패드를 덮어써도 홀드/에지가 풀리지 않는다. */
        input_event_t pad;
        bool pad_live = s_gp_ready && s_gp_last_ms != 0 &&
                        (now_ms - s_gp_last_ms) <= FAILSAFE_MS;
        if (pad_live)
            pad = s_last_gp_evt;
        else
            pad = evt;

        uint8_t l1 = (pad.buttons & BUTTON_SHOULDER_L) ? 1 : 0;
        uint8_t r1 = (pad.buttons & BUTTON_SHOULDER_R) ? 1 : 0;
        if (l1 && r1) {
            if (wheel_test_pressed_at == 0) {
                wheel_test_pressed_at = now_ms;
            }
            if (!wheel_test_fired &&
                !rccar_motor_wheel_test_is_running() &&
                now_ms - wheel_test_pressed_at >= WHEEL_TEST_HOLD_MS) {
                wheel_test_fired = true;
                rccar_motor_wheel_test_start();
                request_rumble(pad.device, 300, 200, 200);
            }
        } else {
            wheel_test_pressed_at = 0;
            wheel_test_fired = false;
        }

        if (rccar_motor_wheel_test_is_running()) {
            rccar_radar_set_moving(true);
        } else if (s_bb_ready) {
            /* 패드 스틱이 들어와도 주행은 보드 스냅샷만 사용한다.
               보드 리포트가 끊기면 스틱으로 대체하지 않고 휠만 멈춘다. */
            if (s_bb_last_ms == 0 || (now_ms - s_bb_last_ms) > FAILSAFE_MS) {
                rccar_motor_wheel_set(0, 0, 0, 0);
                maybe_idle_exhaust(false, now_ms);
            } else {
                apply_balance_board_drive(s_bb_axis_y, s_bb_axis_rx, now_ms);
            }
        } else if (!evt.balance_board) {
        int32_t ax = clamp_axis(evt.axis_x);
        int32_t ay = clamp_axis(evt.axis_y);
        int32_t arx = clamp_axis(evt.axis_rx);
        int32_t ary = clamp_axis(evt.axis_ry);

        /* Left stick: car-like drive (forward/back + yaw) */
        int32_t vx = STICK_VX_SIGN * rccar_drive_apply_deadzone(ay, AXIS_DEADZONE);
        int32_t w = STICK_W_SIGN * rccar_drive_apply_deadzone(ax, AXIS_DEADZONE);

        /* Right stick: body-frame translation, no yaw. 대각선 구간은 45°로 맞춘다. */
        int32_t r_vx = STICK_RY_VX_SIGN * ary;
        int32_t r_vy = STICK_VY_SIGN * arx;
        rccar_drive_snap_diagonal(&r_vx, &r_vy, AXIS_DEADZONE);
        vx += r_vx;
        int32_t vy = r_vy;

        rccar_wheel_speeds_t wheels;
        rccar_drive_mix(vx, vy, w, &wheels);
        log_drive_mix(vx, vy, w, &wheels);
        rccar_motor_wheel_set(wheels.fl, wheels.fr, wheels.rl, wheels.rr);
        maybe_idle_exhaust(wheels.fl != 0 || wheels.fr != 0 ||
                           wheels.rl != 0 || wheels.rr != 0, now_ms);
        } else {
            rccar_motor_wheel_set(0, 0, 0, 0);
            maybe_idle_exhaust(false, now_ms);
        }

        if (!rccar_motor_wheel_test_is_running()) {
            if (pad_live || !evt.balance_board) {
                int32_t turret = 0;
                if (pad.dpad & DPAD_LEFT)
                    turret = -TURRET_SPEED;
                if (pad.dpad & DPAD_RIGHT)
                    turret = TURRET_SPEED;
                rccar_motor_turret_set(turret);
            } else {
                rccar_motor_turret_set(0);
            }
        }

        /* Y edge: 레이더 서보 ON/OFF 토글 (기본 OFF) */
        if ((pad.buttons & BUTTON_Y) && !(prev_buttons & BUTTON_Y)) {
            if (now_ms - last_y_ms >= RADAR_DEBOUNCE_MS) {
                last_y_ms = now_ms;
                rccar_radar_toggle();
            }
        }

        /* A edge: 개틀링 발사 (효과음 + LED 점멸) */
        if ((pad.buttons & BUTTON_A) && !(prev_buttons & BUTTON_A)) {
            if (now_ms - last_a_ms >= GATLING_DEBOUNCE_MS) {
                last_a_ms = now_ms;
                handle_a_button_gatling(pad.device);
            }
        }

        /* Select edge: 헤드라이트 토글. Start와 같이 누르면 공장초기화용이므로 무시 */
        if ((pad.misc_buttons & MISC_BUTTON_SELECT) &&
            !(prev_misc_buttons & MISC_BUTTON_SELECT) &&
            !(pad.misc_buttons & MISC_BUTTON_START)) {
            if (now_ms - last_select_ms >= HEADLIGHT_DEBOUNCE_MS) {
                last_select_ms = now_ms;
                rccar_headlight_toggle();
            }
        }

        /* B edge: 레이저 발사 (효과음 + LED, 후좌 없음) */
        if ((pad.buttons & BUTTON_B) && !(prev_buttons & BUTTON_B)) {
            if (now_ms - last_b_ms >= LASER_DEBOUNCE_MS) {
                last_b_ms = now_ms;
                handle_b_button_fire(pad.device);
            }
        }

        /* L1 / R1: volume - / + (동시 누름은 휠 테스트용) */
        if (!(l1 && r1)) {
        if (pad.buttons & BUTTON_SHOULDER_L) {
            if (now_ms - last_l1_ms >= DEBOUNCE_MS) {
                last_l1_ms = now_ms;
                uint8_t v = rccar_storage_volume_get();
                if (v > RCCAR_VOLUME_MIN) {
                    v--;
                    rccar_storage_volume_set(v);
                    rccar_dfplayer_set_volume(v);
                }
            }
        }

        if (pad.buttons & BUTTON_SHOULDER_R) {
            if (now_ms - last_r1_ms >= DEBOUNCE_MS) {
                last_r1_ms = now_ms;
                uint8_t v = rccar_storage_volume_get();
                if (v < RCCAR_VOLUME_MAX) {
                    v++;
                    rccar_storage_volume_set(v);
                    rccar_dfplayer_set_volume(v);
                }
            }
        }
        }

        /* Select + Start hold: factory reset (NVS erase + reboot) */
        uint8_t sel = (pad.misc_buttons & MISC_BUTTON_SELECT) ? 1 : 0;
        uint8_t sta = (pad.misc_buttons & MISC_BUTTON_START) ? 1 : 0;
        if (sel && sta) {
            if (select_start_pressed_at == 0)
                select_start_pressed_at = now_ms;
            if (!select_start_fired && now_ms - select_start_pressed_at >= SELECT_START_HOLD_MS) {
                select_start_fired = true;
                request_rumble(pad.device, 800, 255, 255);
                esp_timer_stop(restart_timer);
                esp_timer_start_once(restart_timer, 800 * 1000);
            }
        } else {
            select_start_pressed_at = 0;
            select_start_fired = false;
        }

        prev_buttons = pad.buttons;
        prev_misc_buttons = pad.misc_buttons;
    }
}

static void my_platform_init(int argc, const char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    logi("custom: init()\n");

    esp_err_t err = rccar_init();
    if (err != ESP_OK) {
        loge("rccar_init failed: %s\n", esp_err_to_name(err));
        return;
    }

    rumble_request.callback = &rumble_on_btstack_thread;
    rumble_request.context = NULL;
    gamepad_effect_request.callback = &gamepad_effect_on_btstack_thread;
    gamepad_effect_request.context = NULL;
    keepalive_request.callback = &gamepad_keepalive_on_btstack_thread;
    keepalive_request.context = NULL;

    input_queue = xQueueCreate(INPUT_QUEUE_LEN, sizeof(input_event_t));
    configASSERT(input_queue);

    BaseType_t ret = xTaskCreatePinnedToCore(
        input_process_task, "input_proc", INPUT_TASK_STACK, NULL,
        INPUT_TASK_PRIO, NULL, 1);
    configASSERT(ret == pdPASS);

    const esp_timer_create_args_t restart_timer_args = {
        .callback = &delayed_restart_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "restart",
    };
    esp_timer_create(&restart_timer_args, &restart_timer);

    const esp_timer_create_args_t waiting_idle_args = {
        .callback = &waiting_idle_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "waiting_idle",
    };
    esp_timer_create(&waiting_idle_args, &waiting_idle_timer);

    const esp_timer_create_args_t connected_idle_bgm_args = {
        .callback = &connected_idle_bgm_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "connected_idle_bgm",
    };
    esp_timer_create(&connected_idle_bgm_args, &connected_idle_bgm_timer);

    const esp_timer_create_args_t connect_sound_args = {
        .callback = &connect_sound_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "connect_sound",
    };
    esp_timer_create(&connect_sound_args, &connect_sound_timer);

    const esp_timer_create_args_t connect_sound_play_args = {
        .callback = &connect_sound_play_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "connect_sound_play",
    };
    esp_timer_create(&connect_sound_play_args, &connect_sound_play_timer);

    const esp_timer_create_args_t gamepad_effect_args = {
        .callback = &gamepad_effect_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gamepad_effect",
    };
    esp_timer_create(&gamepad_effect_args, &gamepad_effect_timer);

    const esp_timer_create_args_t gamepad_keepalive_args = {
        .callback = &gamepad_keepalive_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gamepad_keepalive",
    };
    esp_timer_create(&gamepad_keepalive_args, &gamepad_keepalive_timer);

    const esp_timer_create_args_t scan_restart_args = {
        .callback = &scan_restart_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "scan_restart",
    };
    esp_timer_create(&scan_restart_args, &scan_restart_timer);

    const esp_timer_create_args_t humidifier_pulse_args = {
        .callback = &humidifier_pulse_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "humidifier_pulse",
    };
    esp_timer_create(&humidifier_pulse_args, &humidifier_pulse_timer);

    const esp_timer_create_args_t laser_rumble_args = {
        .callback = &laser_rumble_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "laser_rumble",
    };
    esp_timer_create(&laser_rumble_args, &laser_rumble_timer);
}

static void my_platform_on_init_complete(void) {
    logi("custom: on_init_complete()\n");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);

    /* DFPlayer는 uni_init() 밖인 여기서 초기화한다. UART 드라이버 설치와 대기를
       BT 스택 초기화 도중에 하면 연결이 불안정해진다 (panzer4/king-tiger와 동일). */
    esp_err_t ret = rccar_dfplayer_init();
    if (ret != ESP_OK) {
        loge("rccar_dfplayer_init failed: %s\n", esp_err_to_name(ret));
        return;
    }

    uint8_t vol = rccar_storage_volume_get();
    rccar_dfplayer_set_volume(vol);
    vTaskDelay(pdMS_TO_TICKS(200));

    rccar_dfplayer_play_loop(RCCAR_DFPLAYER_TRACK_IDLE);
    esp_timer_start_periodic(waiting_idle_timer, 30 * 1000 * 1000);
}

static uni_error_t my_platform_on_device_discovered(bd_addr_t addr, const char *name, uint16_t cod, uint8_t rssi) {
    (void)addr;
    (void)name;
    (void)rssi;
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        logi("Ignoring keyboard\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t *d) {
    logi("custom: device connected: %p\n", d);
    /* 가상 자식 장치(DS4 터치패드 마우스 등)는 무시한다. 이 차는 게임패드만 쓴다. */
    if (d->parent != NULL) {
        logi("custom: ignoring virtual device\n");
        return;
    }
    my_platform_instance_t *ins = get_my_platform_instance(d);
    ins->ready = false;
    /* inquiry(주기적 스캔)는 BR/EDR 대역을 크게 점유한다. 연결 직후 HID 셋업과
       첫 출력 리포트가 오가는 구간에 겹치면 링크가 굶어 컨트롤러가 끊는다.
       Bluepad32는 장치가 다 차도 스캔을 자동으로 끄지 않으므로 여기서 끈다.
       플랫폼 콜백은 btstack 스레드이므로 _unsafe 변형을 쓴다. */
    /* 유예 중에 재연결이 성공했다면 예약된 스캔 재시작을 취소한다. */
    esp_timer_stop(scan_restart_timer);
    uni_bt_stop_scanning_unsafe();

    /* 수신 연결은 계속 허용한다. Xbox Wireless 계열은 링크가 살아 있어도 새
       연결을 여는데(uni_bt_bredr.c 주석 참고), 이를 거절하면 컨트롤러가 기존
       링크를 스스로 끊어버린다. Bluepad32의 "existing connection" 처리가
       이 경우의 복구 경로다. */
}

static void my_platform_on_device_disconnected(uni_hid_device_t *d) {
    logi("custom: device disconnected: %p\n", d);
    /* 가상 자식 장치가 끊긴 것은 게임패드 연결과 무관하다. 여기서 s_connected를
       내리면 진짜 패드가 붙어 있는데도 입력이 영원히 무시된다. */
    if (d->parent != NULL) {
        logi("custom: ignoring virtual device\n");
        return;
    }

    my_platform_instance_t *ins = get_my_platform_instance(d);
    bool was_ready = ins->ready;
    bool was_bb = (d == s_bb_device);
    bool was_gp = (d == s_gp_device);
    if (was_ready) {
        ins->ready = false;
        if (s_ready_count > 0)
            s_ready_count--;
    }
    forget_ready_device(d);

    if (d == gamepad_effect_device) {
        gamepad_effect_device = NULL;
        esp_timer_stop(gamepad_effect_timer);
    }
    if (d == keepalive_device)
        gamepad_keepalive_stop();

    /* 다른 장치가 남아 있으면 전체 페일세이프를 하지 않는다. */
    if (s_ready_count > 0) {
        s_connected = true;
        if (was_bb)
            rccar_motor_wheel_set(0, 0, 0, 0);
        if (was_gp)
            rccar_motor_turret_set(0);
        maybe_schedule_scan();
        return;
    }

    /* Drop connection first so Core1 stops applying any late samples */
    s_connected = false;
    s_gamepad_prev_buttons = 0;
    esp_timer_stop(humidifier_pulse_timer);
    esp_timer_stop(laser_rumble_timer);
    laser_rumble_device = NULL;
    esp_timer_stop(connect_sound_timer);
    if (connect_sound_play_timer != NULL) {
        esp_timer_stop(connect_sound_play_timer);
    }
    failsafe_stop();
    if (input_queue != NULL)
        xQueueReset(input_queue);
    connected_idle_bgm_reset();
    rccar_dfplayer_play_loop(RCCAR_DFPLAYER_TRACK_IDLE);
    esp_timer_start_periodic(waiting_idle_timer, 30 * 1000 * 1000);

    /* 스캔은 바로 켜지 않는다. 컨트롤러가 스스로 재연결하는 구간에 inquiry가
       겹치면 그 절차를 방해한다. 유예 후에도 연결이 없으면 그때 켠다. */
    esp_timer_stop(scan_restart_timer);
    esp_timer_start_once(scan_restart_timer, SCAN_RESTART_DELAY_MS * 1000);
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t *d) {
    logi("custom: device ready: %p\n", d);
    /* 가상 자식 장치는 받지 않는다. 거부하면 Bluepad32가 부모와의 링크를 끊는다
       (uni_hid_parser_ds4.c: "platform rejects the virtual device"). */
    if (d->parent != NULL) {
        logi("custom: rejecting virtual device\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }
    my_platform_instance_t *ins = get_my_platform_instance(d);
    ins->gamepad_seat = GAMEPAD_SEAT_A;
    memset(&ins->bb_state, 0, sizeof(ins->bb_state));

    bool first = (s_ready_count == 0);
    bool is_bb = device_is_balance_board(d);
    if (is_bb) {
        s_bb_device = d;
        s_bb_ready = true;
        s_bb_axis_y = 0;
        s_bb_axis_rx = 0;
        s_bb_last_ms = 0;
        logi("custom: Wii Balance Board ready\n");
    } else {
        s_gp_device = d;
        s_gp_ready = true;
    }
    ins->ready = true;
    s_ready_count++;

    if (first) {
        /* Ensure motors stopped before accepting input */
        rccar_motor_all_stop();
        if (input_queue != NULL)
            xQueueReset(input_queue);

        esp_timer_stop(waiting_idle_timer);
        esp_timer_stop(connect_sound_timer);
        if (connect_sound_play_timer != NULL) {
            esp_timer_stop(connect_sound_play_timer);
        }
        esp_timer_start_once(connect_sound_timer, 100 * 1000);
    }

    /* 럼블/LED는 trigger_event_on_gamepad 한 번으로 끝낸다. DS4는 calibration/fw
       feature report 교환 중 출력 리포트를 받으면 링크를 끊을 수 있으므로 지연한다.
       보드가 두 번째로 붙을 때는 패드의 keep-alive를 덮어쓰지 않는다. */
    if (!is_bb || first) {
        gamepad_effect_device = d;
        esp_timer_stop(gamepad_effect_timer);
        esp_timer_start_once(gamepad_effect_timer, GAMEPAD_EFFECT_DELAY_MS * 1000);
    }
    if (!is_bb)
        gamepad_keepalive_start(d);

    s_connected = true;
    connected_idle_bgm_reset();
    maybe_schedule_scan();
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl) {
    if (!s_connected || input_queue == NULL)
        return;

    input_event_t evt = {
        .device = d,
        .timestamp_ms = esp_timer_get_time() / 1000,
    };

    switch (ctl->klass) {
        case UNI_CONTROLLER_CLASS_GAMEPAD: {
            uint16_t buttons = ctl->gamepad.buttons;
            if ((buttons & BUTTON_X) && !(s_gamepad_prev_buttons & BUTTON_X))
                handle_x_button_press(d);
            s_gamepad_prev_buttons = buttons;

            evt.axis_x = ctl->gamepad.axis_x;
            evt.axis_y = ctl->gamepad.axis_y;
            evt.axis_rx = ctl->gamepad.axis_rx;
            evt.axis_ry = ctl->gamepad.axis_ry;
            evt.dpad = ctl->gamepad.dpad;
            evt.buttons = buttons;
            evt.misc_buttons = ctl->gamepad.misc_buttons;
            s_last_gp_evt = evt;
            s_gp_last_ms = evt.timestamp_ms;
            break;
        }
        case UNI_CONTROLLER_CLASS_BALANCE_BOARD: {
            my_platform_instance_t *ins = get_my_platform_instance(d);
            int32_t bb_x = 0;
            int32_t bb_y = 0;
            balance_board_to_stick_axes(&ctl->balance_board, &ins->bb_state, &bb_x, &bb_y);
            evt.axis_x = 0;
            evt.axis_y = bb_y;
            evt.axis_rx = bb_x;
            evt.axis_ry = 0;
            evt.balance_board = true;
            s_bb_axis_y = bb_y;
            s_bb_axis_rx = bb_x;
            s_bb_last_ms = evt.timestamp_ms;
            break;
        }
        default:
            return;
    }

    xQueueOverwrite(input_queue, &evt);
}

static const uni_property_t *my_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_oob_event(uni_platform_oob_event_t event, void *data) {
    switch (event) {
        case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON: {
            uni_hid_device_t *d = data;
            if (d == NULL) {
                loge("ERROR: my_platform_on_oob_event: Invalid NULL device\n");
                return;
            }
            my_platform_instance_t *ins = get_my_platform_instance(d);
            ins->gamepad_seat = ins->gamepad_seat == GAMEPAD_SEAT_A ? GAMEPAD_SEAT_B : GAMEPAD_SEAT_A;
            trigger_event_on_gamepad(d);
            break;
        }
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
            logi("custom: Bluetooth enabled: %d\n", (bool)(data));
            break;
        default:
            logi("my_platform_on_oob_event: unsupported event: 0x%04x\n", event);
            break;
    }
}

static my_platform_instance_t *get_my_platform_instance(uni_hid_device_t *d) {
    return (my_platform_instance_t *)&d->platform_data[0];
}

static void trigger_event_on_gamepad(uni_hid_device_t *d) {
    my_platform_instance_t *ins = get_my_platform_instance(d);
    if (d->report_parser.set_player_leds != NULL)
        d->report_parser.set_player_leds(d, ins->gamepad_seat);
    if (d->report_parser.set_lightbar_color != NULL) {
        uint8_t red = (ins->gamepad_seat & 0x01) ? 0xff : 0;
        uint8_t green = (ins->gamepad_seat & 0x02) ? 0xff : 0;
        uint8_t blue = (ins->gamepad_seat & 0x04) ? 0xff : 0;
        d->report_parser.set_lightbar_color(d, red, green, blue);
    }
}

struct uni_platform *get_my_platform(void) {
    static struct uni_platform plat = {
        .name = "custom",
        .init = my_platform_init,
        .on_init_complete = my_platform_on_init_complete,
        .on_device_discovered = my_platform_on_device_discovered,
        .on_device_connected = my_platform_on_device_connected,
        .on_device_disconnected = my_platform_on_device_disconnected,
        .on_device_ready = my_platform_on_device_ready,
        .on_oob_event = my_platform_on_oob_event,
        .on_controller_data = my_platform_on_controller_data,
        .get_property = my_platform_get_property,
    };
    return &plat;
}
