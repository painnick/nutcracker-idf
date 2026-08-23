// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Ricardo Quesada
// http://retro.moe/unijoysticle2

// For more info about Android mappings see:
// https://developer.android.com/training/game-controllers/controller-input

#include "parser/uni_hid_parser_android.h"

#include <string.h>

#include "btstack_run_loop.h"
#include "controller/uni_controller.h"
#include "hid_usage.h"
#include "uni_common.h"
#include "uni_hid_device.h"
#include "uni_log.h"

/* ShanWan / Fire TV style output report (report ID 0x02). See Linux hid-shanwan.c */
#define ANDROID_RUMBLE_REPORT_ID 0x02

enum {
    ANDROID_STATE_RUMBLE_DISABLED,
    ANDROID_STATE_RUMBLE_DELAYED,
    ANDROID_STATE_RUMBLE_IN_PROGRESS,
};

typedef struct __attribute__((packed)) {
    uint8_t transaction_type;  /* 0xa2 = DATA | OUTPUT on interrupt channel */
    uint8_t report_id;
    uint8_t msg;       /* 0x02 = rumble */
    uint8_t reserved;  /* always 0x08 */
    uint8_t motor_a;   /* weak motor (Linux: weak_magnitude) */
    uint8_t motor_b;   /* strong motor */
    uint8_t duration;  /* 0xff = until cleared */
    uint8_t pad[3];
} android_ff_report_t;

typedef struct android_instance_s {
    btstack_timer_source_t rumble_timer_duration;
    btstack_timer_source_t rumble_timer_delayed_start;
    int rumble_state;
    uint16_t rumble_weak_magnitude;
    uint16_t rumble_strong_magnitude;
    uint16_t rumble_duration_ms;
} android_instance_t;
_Static_assert(sizeof(android_instance_t) < HID_DEVICE_MAX_PARSER_DATA, "Android instance too big");

static android_instance_t* get_android_instance(uni_hid_device_t* d);
static void android_stop_rumble_now(uni_hid_device_t* d);
static void android_send_ff_report(uni_hid_device_t* d, uint8_t weak_magnitude, uint8_t strong_magnitude,
                                   uint8_t duration);
static void android_play_dual_rumble_now(uni_hid_device_t* d,
                                         uint16_t duration_ms,
                                         uint8_t weak_magnitude,
                                         uint8_t strong_magnitude);
static void on_android_set_rumble_on(btstack_timer_source_t* ts);
static void on_android_set_rumble_off(btstack_timer_source_t* ts);

void uni_hid_parser_android_init_report(uni_hid_device_t* d) {
    // Reset old state. Each report contains a full-state.
    uni_controller_t* ctl = &d->controller;
    memset(ctl, 0, sizeof(*ctl));
    ctl->klass = UNI_CONTROLLER_CLASS_GAMEPAD;
}

void uni_hid_parser_android_parse_usage(uni_hid_device_t* d,
                                        const hid_globals_t* globals,
                                        uint16_t usage_page,
                                        uint16_t usage,
                                        int32_t value) {
    uint8_t hat;
    uni_controller_t* ctl = &d->controller;
    switch (usage_page) {
        case HID_USAGE_PAGE_GENERIC_DESKTOP:
            switch (usage) {
                case HID_USAGE_AXIS_X:
                    ctl->gamepad.axis_x = uni_hid_parser_process_axis(globals, value);
                    break;
                case HID_USAGE_AXIS_Y:
                    ctl->gamepad.axis_y = uni_hid_parser_process_axis(globals, value);
                    break;
                case HID_USAGE_AXIS_Z:
                    ctl->gamepad.axis_rx = uni_hid_parser_process_axis(globals, value);
                    break;
                case HID_USAGE_AXIS_RZ:
                    ctl->gamepad.axis_ry = uni_hid_parser_process_axis(globals, value);
                    break;
                case HID_USAGE_HAT:
                    hat = uni_hid_parser_process_hat(globals, value);
                    ctl->gamepad.dpad = uni_hid_parser_hat_to_dpad(hat);
                    break;
                case HID_USAGE_DPAD_UP:
                case HID_USAGE_DPAD_DOWN:
                case HID_USAGE_DPAD_RIGHT:
                case HID_USAGE_DPAD_LEFT:
                    uni_hid_parser_process_dpad(usage, value, &ctl->gamepad.dpad);
                    break;
                default:
                    // Only report unsupported values if they are 1.
                    if (value)
                        logi("Android: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage,
                             value);
                    break;
            }
            break;
        case HID_USAGE_PAGE_SIMULATION_CONTROLS:
            switch (usage) {
                case HID_USAGE_ACCELERATOR:
                    ctl->gamepad.throttle = uni_hid_parser_process_pedal(globals, value);
                    break;
                case HID_USAGE_BRAKE:
                    ctl->gamepad.brake = uni_hid_parser_process_pedal(globals, value);
                    break;
                default:
                    // Only report unsupported values if they are 1.
                    if (value)
                        logi("Android: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage,
                             value);
                    break;
            }
            break;
        case HID_USAGE_PAGE_GENERIC_DEVICE_CONTROLS:
            switch (usage) {
                case HID_USAGE_BATTERY_STRENGTH:
                    ctl->battery = value;
                    break;
                default:
                    if (value)
                        logi("Android: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage,
                             value);
                    break;
            }
            break;
        case HID_USAGE_PAGE_BUTTON: {
            switch (usage) {
                case 0x01:  // Button A
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_A;
                    break;
                case 0x02:  // Button B
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_B;
                    break;
                case 0x03:  // non-existant button C?
                    // unmapped
                    break;
                case 0x04:  // Button X
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_X;
                    break;
                case 0x05:  // Button Y
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_Y;
                    break;
                case 0x06:  // non-existant button Z?
                    // unmapped
                    break;
                case 0x07:
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_SHOULDER_L;
                    break;
                case 0x08:
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_SHOULDER_R;
                    break;
                case 0x09:
                    // Available on some Android gamepads like SteelSeries Stratus Duo.
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_TRIGGER_L;
                    break;
                case 0x0a:
                    // Available on some Android gamepads like SteelSeries Stratus Duo.
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_TRIGGER_R;
                    break;
                case 0x0b:
                    // Available on some Android gamepads like SteelSeries Stratus Duo.
                    if (value)
                        ctl->gamepad.misc_buttons |= MISC_BUTTON_SELECT;
                    break;
                case 0x0c:
                    // Available on some Android gamepads like SteelSeries Stratus Duo.
                    if (value)
                        ctl->gamepad.misc_buttons |= MISC_BUTTON_START;
                    break;
                case 0x0d:
                    if (value)
                        ctl->gamepad.misc_buttons |= MISC_BUTTON_SYSTEM;
                    break;
                case 0x0e:
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_THUMB_L;
                    break;
                case 0x0f:
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_THUMB_R;
                    break;
                case 0x11:
                    // Stadia controller: Capture button
                    break;
                case 0x12:
                    // Stadia controller: Google Assistant button
                    break;
                case 0x13:
                    // Stadia: LT. It reports both LT and Brake
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_TRIGGER_L;
                    break;
                case 0x14:
                    // Stadia: RT. It reports both RT and Gas
                    if (value)
                        ctl->gamepad.buttons |= BUTTON_TRIGGER_R;
                    break;
                default:
                    // Only report unsupported values if they are 1.
                    if (value)
                        logi(
                            "Android: Unsupported page: 0x%04x, usage: 0x%04x, "
                            "value=0x%x\n",
                            usage_page, usage, value);
                    break;
            }
            break;
        }
        case HID_USAGE_PAGE_CONSUMER:
            switch (usage) {
                case HID_USAGE_AC_HOME:
                    // FIXME: Some devices, like SteelSeries Status Duo, use this value as
                    // BUTTON_SYSTEM. But others like Asus, use this one to report
                    // BUTTON_START. Instead of having a parser for Android / OUYA /
                    // 8Bitdo, we should have a HID parser and then mapping files for each
                    // VID / PID (similar to Android .kl files).
                    if (value)
                        ctl->gamepad.misc_buttons |= MISC_BUTTON_START;
                    break;
                case HID_USAGE_AC_BACK:
                    if (value)
                        ctl->gamepad.misc_buttons |= MISC_BUTTON_SELECT;
                    break;
                default:
                    // Only report unsupported values if they are 1.
                    if (value)
                        logi("Android: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage,
                             value);
                    break;
            }
            break;

        case 0xff01:
            // Ignore this report. Used by some Moga devices, where page 0xff01,
            // usage: 0x0001 is always 1.
            if (usage == 0x01)
                break;
            break;

        default:
            // Only report unsupported values if they are 1.
            if (value)
                logi("Android: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage, value);
            break;
    }
}

void uni_hid_parser_android_play_dual_rumble(uni_hid_device_t* d,
                                             uint16_t start_delay_ms,
                                             uint16_t duration_ms,
                                             uint8_t weak_magnitude,
                                             uint8_t strong_magnitude) {
    if (d == NULL) {
        loge("Android: Invalid device\n");
        return;
    }

    android_instance_t* ins = get_android_instance(d);
    switch (ins->rumble_state) {
        case ANDROID_STATE_RUMBLE_DELAYED:
            btstack_run_loop_remove_timer(&ins->rumble_timer_delayed_start);
            break;
        case ANDROID_STATE_RUMBLE_IN_PROGRESS:
            btstack_run_loop_remove_timer(&ins->rumble_timer_duration);
            break;
        default:
            break;
    }

    if (duration_ms == 0) {
        android_send_ff_report(d, 0, 0, 0);
        if (ins->rumble_state != ANDROID_STATE_RUMBLE_DISABLED) {
            btstack_run_loop_remove_timer(&ins->rumble_timer_duration);
            ins->rumble_state = ANDROID_STATE_RUMBLE_DISABLED;
        }
        return;
    }

    ins->rumble_weak_magnitude = weak_magnitude;
    ins->rumble_strong_magnitude = strong_magnitude;
    ins->rumble_duration_ms = duration_ms;

    if (start_delay_ms == 0) {
        android_play_dual_rumble_now(d, duration_ms, weak_magnitude, strong_magnitude);
        return;
    }

    ins->rumble_timer_delayed_start.process = &on_android_set_rumble_on;
    ins->rumble_timer_delayed_start.context = d;
    ins->rumble_state = ANDROID_STATE_RUMBLE_DELAYED;
    btstack_run_loop_set_timer(&ins->rumble_timer_delayed_start, start_delay_ms);
    btstack_run_loop_add_timer(&ins->rumble_timer_delayed_start);
}

void uni_hid_parser_android_set_player_leds(uni_hid_device_t* d, uint8_t leds) {
#if 0
  static uint8_t report_id = 0;
  logi("using report id = 0x%02x\n", report_id);
  uint8_t report[] = {0xa2, 0, 0x00 /* LED */};
  report[2] = 0x02; /* d->joystick_port; */
  report[1] = report_id++;
  uni_hid_device_queue_report(d, report, sizeof(report));
  report[0] = 0x52;
  uni_hid_device_queue_report(d, report, sizeof(report));
#else
    ARG_UNUSED(d);
    ARG_UNUSED(leds);
#endif
}

static android_instance_t* get_android_instance(uni_hid_device_t* d) {
    return (android_instance_t*)&d->parser_data[0];
}

static void android_send_ff_report(uni_hid_device_t* d,
                                   uint8_t weak_magnitude,
                                   uint8_t strong_magnitude,
                                   uint8_t duration) {
    const android_ff_report_t ff = {
        .transaction_type = 0xa2,
        .report_id = ANDROID_RUMBLE_REPORT_ID,
        .msg = 0x02,
        .reserved = 0x08,
        .motor_a = weak_magnitude,
        .motor_b = strong_magnitude,
        .duration = duration,
        .pad = {0, 0, 0},
    };
    uni_hid_device_send_intr_report(d, (const uint8_t*)&ff, sizeof(ff));
}

static void android_stop_rumble_now(uni_hid_device_t* d) {
    android_instance_t* ins = get_android_instance(d);
    ins->rumble_state = ANDROID_STATE_RUMBLE_DISABLED;
    android_send_ff_report(d, 0, 0, 0);
}

static void android_play_dual_rumble_now(uni_hid_device_t* d,
                                         uint16_t duration_ms,
                                         uint8_t weak_magnitude,
                                         uint8_t strong_magnitude) {
    android_instance_t* ins = get_android_instance(d);

    if (duration_ms == 0) {
        if (ins->rumble_state != ANDROID_STATE_RUMBLE_DISABLED)
            android_stop_rumble_now(d);
        return;
    }

    android_send_ff_report(d, weak_magnitude, strong_magnitude, 0xff);
    ins->rumble_timer_duration.process = &on_android_set_rumble_off;
    ins->rumble_timer_duration.context = d;
    ins->rumble_state = ANDROID_STATE_RUMBLE_IN_PROGRESS;
    btstack_run_loop_set_timer(&ins->rumble_timer_duration, duration_ms);
    btstack_run_loop_add_timer(&ins->rumble_timer_duration);
}

static void on_android_set_rumble_on(btstack_timer_source_t* ts) {
    uni_hid_device_t* d = ts->context;
    android_instance_t* ins = get_android_instance(d);
    android_play_dual_rumble_now(d, ins->rumble_duration_ms, (uint8_t)ins->rumble_weak_magnitude,
                                 (uint8_t)ins->rumble_strong_magnitude);
}

static void on_android_set_rumble_off(btstack_timer_source_t* ts) {
    uni_hid_device_t* d = ts->context;
    android_stop_rumble_now(d);
}
