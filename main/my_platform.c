// RC Car platform - Bluepad32 + rccar
// 입력 처리를 Core 1로 오프로딩하여 Core 0 BT 컨트롤러 부하 경감
//
// Stick sign conventions (Bluepad32 / typical HID, after deadzone):
//   axis_y: up is negative -> invert so up = +vx (forward)
//   axis_x: right is positive -> +vy (strafe right)
//   axis_rx: right is positive -> +w (yaw CW looking from above)
// Adjust STICK_*_SIGN below if real hardware orientation differs.

#include <string.h>

#include <platform/uni_platform.h>
#include <uni.h>
#include "controller/uni_controller.h"
#include "controller/uni_gamepad.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "rccar.h"
#include "rccar_dfplayer.h"
#include "rccar_drive.h"
#include "rccar_led.h"
#include "rccar_motor.h"
#include "rccar_servo.h"
#include "rccar_storage.h"

#define AXIS_MAX 512
#define AXIS_DEADZONE 60
#define FAILSAFE_MS 1000
#define TURRET_SPEED 511
#define DEBOUNCE_MS 100
#define WARM_WHITE_DEBOUNCE_MS 400
#define SELECT_START_HOLD_MS 3000

/* Stick axis polarity: multiply raw (post-deadzone) value. */
#define STICK_VX_SIGN (-1) /* axis_y: invert so stick-up = forward */
#define STICK_VY_SIGN (1)  /* axis_x: stick-right = strafe right */
#define STICK_W_SIGN (1)   /* axis_rx: stick-right = yaw CW */

#define INPUT_QUEUE_LEN 1
#define INPUT_TASK_STACK 4096
#define INPUT_TASK_PRIO 5
#define INPUT_POLL_MS 50

typedef struct my_platform_instance_s {
    uni_gamepad_seat_t gamepad_seat;
} my_platform_instance_t;

typedef struct {
    int32_t axis_x;  /* left stick X = vy */
    int32_t axis_y;  /* left stick Y = vx */
    int32_t axis_rx; /* right stick X = w */
    uint16_t dpad;
    uint16_t buttons;
    uint8_t misc_buttons;
    uni_hid_device_t *device;
    int64_t timestamp_ms;
} input_event_t;

static void trigger_event_on_gamepad(uni_hid_device_t *d);
static my_platform_instance_t *get_my_platform_instance(uni_hid_device_t *d);

static QueueHandle_t input_queue = NULL;
static esp_timer_handle_t restart_timer = NULL;
static esp_timer_handle_t waiting_idle_timer = NULL;
static volatile bool s_radar_armed = false;

static void waiting_idle_cb(void *arg) {
    (void)arg;
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_IDLE);
}

static void delayed_restart_cb(void *arg) {
    (void)arg;
    rccar_storage_erase_and_restart();
}

static int32_t clamp_axis(int32_t v) {
    if (v > 511)
        return 511;
    if (v < -512)
        return -512;
    return v;
}

static void failsafe_stop(void) {
    rccar_motor_all_stop();
    s_radar_armed = false;
    rccar_radar_set_armed(false);
}

static void input_process_task(void *arg) {
    (void)arg;

    static int64_t last_y_ms = 0;
    static int64_t last_l1_ms = 0;
    static int64_t last_r1_ms = 0;
    static int64_t select_start_pressed_at = 0;
    static uint16_t prev_buttons = 0;
    static int64_t last_input_ms = 0;
    static bool failsafe_active = true;

    input_event_t evt;

    while (1) {
        BaseType_t got = xQueueReceive(input_queue, &evt, pdMS_TO_TICKS(INPUT_POLL_MS));
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (got == pdTRUE) {
            last_input_ms = evt.timestamp_ms;
        }

        /* No report for FAILSAFE_MS: stop drive/turret/radar */
        if (last_input_ms == 0 || (now_ms - last_input_ms) > FAILSAFE_MS) {
            if (!failsafe_active) {
                failsafe_stop();
                failsafe_active = true;
            }
            continue;
        }

        if (got != pdTRUE)
            continue;

        failsafe_active = false;

        int32_t ax = clamp_axis(evt.axis_x);
        int32_t ay = clamp_axis(evt.axis_y);
        int32_t arx = clamp_axis(evt.axis_rx);

        int32_t vx = STICK_VX_SIGN * rccar_drive_apply_deadzone(ay, AXIS_DEADZONE);
        int32_t vy = STICK_VY_SIGN * rccar_drive_apply_deadzone(ax, AXIS_DEADZONE);
        int32_t w = STICK_W_SIGN * rccar_drive_apply_deadzone(arx, AXIS_DEADZONE);

        rccar_wheel_speeds_t wheels;
        rccar_drive_mix(vx, vy, w, &wheels);
        rccar_motor_wheel_set(wheels.fl, wheels.fr, wheels.rl, wheels.rr);

        int32_t turret = 0;
        if (evt.dpad & DPAD_LEFT)
            turret = -TURRET_SPEED;
        if (evt.dpad & DPAD_RIGHT)
            turret = TURRET_SPEED;
        rccar_motor_turret_set(turret);

        /* Y edge: warm white toggle */
        if (evt.buttons & BUTTON_Y) {
            if (!(prev_buttons & BUTTON_Y) && (now_ms - last_y_ms >= WARM_WHITE_DEBOUNCE_MS)) {
                last_y_ms = now_ms;
                rccar_led_warm_white_toggle();
            }
        }

        /* X edge: radar armed toggle */
        if (evt.buttons & BUTTON_X) {
            if (!(prev_buttons & BUTTON_X)) {
                s_radar_armed = !s_radar_armed;
                rccar_radar_set_armed(s_radar_armed);
            }
        }

        /* L1 / R1: volume - / + */
        if (evt.buttons & BUTTON_SHOULDER_L) {
            if (now_ms - last_l1_ms >= DEBOUNCE_MS) {
                last_l1_ms = now_ms;
                uint8_t v = rccar_storage_volume_get();
                if (v > RCCAR_VOLUME_MIN) {
                    v--;
                    rccar_storage_volume_set(v);
                    rccar_dfplayer_set_volume(v);
                }
            }
        } else {
            if (prev_buttons & BUTTON_SHOULDER_L)
                rccar_storage_volume_set(rccar_storage_volume_get());
        }

        if (evt.buttons & BUTTON_SHOULDER_R) {
            if (now_ms - last_r1_ms >= DEBOUNCE_MS) {
                last_r1_ms = now_ms;
                uint8_t v = rccar_storage_volume_get();
                if (v < RCCAR_VOLUME_MAX) {
                    v++;
                    rccar_storage_volume_set(v);
                    rccar_dfplayer_set_volume(v);
                }
            }
        } else {
            if (prev_buttons & BUTTON_SHOULDER_R)
                rccar_storage_volume_set(rccar_storage_volume_get());
        }

        /* Select + Start hold: factory reset (NVS erase + reboot) */
        uint8_t sel = (evt.misc_buttons & MISC_BUTTON_SELECT) ? 1 : 0;
        uint8_t sta = (evt.misc_buttons & MISC_BUTTON_START) ? 1 : 0;
        if (sel && sta) {
            if (select_start_pressed_at == 0)
                select_start_pressed_at = now_ms;
            if (now_ms - select_start_pressed_at >= SELECT_START_HOLD_MS) {
                uni_hid_device_t *d = evt.device;
                if (d != NULL && d->report_parser.play_dual_rumble != NULL)
                    d->report_parser.play_dual_rumble(d, 0, 800, 255, 255);
                esp_timer_stop(restart_timer);
                esp_timer_start_once(restart_timer, 800 * 1000);
            }
        } else {
            select_start_pressed_at = 0;
        }

        prev_buttons = evt.buttons;
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
}

static void my_platform_on_init_complete(void) {
    logi("custom: on_init_complete()\n");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);

    uint8_t vol = rccar_storage_volume_get();
    rccar_dfplayer_set_volume(vol);
    vTaskDelay(pdMS_TO_TICKS(200));

    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_IDLE);
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
}

static void my_platform_on_device_disconnected(uni_hid_device_t *d) {
    logi("custom: device disconnected: %p\n", d);
    failsafe_stop();
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_IDLE);
    esp_timer_start_periodic(waiting_idle_timer, 30 * 1000 * 1000);
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t *d) {
    logi("custom: device ready: %p\n", d);
    my_platform_instance_t *ins = get_my_platform_instance(d);
    ins->gamepad_seat = GAMEPAD_SEAT_A;

    /* Ensure motors stopped on connect, then play CONNECT */
    rccar_motor_all_stop();
    rccar_radar_set_armed(false);

    esp_timer_stop(waiting_idle_timer);
    rccar_dfplayer_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_CONNECT);

    trigger_event_on_gamepad(d);
    if (d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, 400, 128, 200);
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
        return;

    if (input_queue == NULL)
        return;

    input_event_t evt = {
        .axis_x = ctl->gamepad.axis_x,
        .axis_y = ctl->gamepad.axis_y,
        .axis_rx = ctl->gamepad.axis_rx,
        .dpad = ctl->gamepad.dpad,
        .buttons = ctl->gamepad.buttons,
        .misc_buttons = ctl->gamepad.misc_buttons,
        .device = d,
        .timestamp_ms = esp_timer_get_time() / 1000,
    };

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
    if (d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, 150, 128, 40);
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
