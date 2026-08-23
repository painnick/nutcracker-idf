// RC Car platform - Bluepad32 + rccar
// 입력 처리를 Core 1로 오프로딩하여 Core 0 BT 컨트롤러 부하 경감
//
// Stick sign conventions (Bluepad32 / typical HID, after deadzone):
//   axis_y: up is negative -> invert so up = +vx (forward)
//   axis_x: right is positive -> +vy (strafe right)
//   axis_rx: right is positive -> +w (yaw CW looking from above)
// Adjust STICK_*_SIGN below if real hardware orientation differs.

#include <string.h>

#include <btstack_run_loop.h>
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
#include "rccar_humidifier.h"
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
#define X_RUMBLE_DURATION_MS 150
#define X_RUMBLE_WEAK 128
#define X_RUMBLE_STRONG 40
#define HUMIDIFIER_PULSE_ON_MS 3000

/* Stick axis polarity: multiply raw (post-deadzone) value. */
#define STICK_VX_SIGN (-1) /* axis_y: invert so stick-up = forward */
#define STICK_VY_SIGN (1)  /* axis_x: stick-right = strafe right */
#define STICK_W_SIGN (1)   /* axis_rx: stick-right = yaw CW */

#define INPUT_QUEUE_LEN 1
#define INPUT_TASK_STACK 4096
#define INPUT_TASK_PRIO 5
#define INPUT_POLL_MS 50
/* DS4 calibration/fw feature report 교환 후에 출력 리포트를 보낸다. */
#define GAMEPAD_EFFECT_DELAY_MS 500

/* 연결이 끊긴 뒤 스캔을 다시 켜기까지의 유예. inquiry는 BR/EDR 대역을 크게
   점유해서, 컨트롤러가 스스로 재연결하려는 순간에 켜면 그 절차를 방해한다.
   Xbox Wireless 계열은 링크가 살아 있어도 새 연결을 여는 특성이 있다. */
#define SCAN_RESTART_DELAY_MS 2000

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
static void request_rumble(uni_hid_device_t *d, uint16_t duration_ms, uint8_t weak, uint8_t strong);
static my_platform_instance_t *get_my_platform_instance(uni_hid_device_t *d);

static QueueHandle_t input_queue = NULL;
static esp_timer_handle_t restart_timer = NULL;
static esp_timer_handle_t waiting_idle_timer = NULL;
static esp_timer_handle_t connect_sound_timer = NULL;
static esp_timer_handle_t gamepad_effect_timer = NULL;
static esp_timer_handle_t scan_restart_timer = NULL;
static esp_timer_handle_t humidifier_pulse_timer = NULL;
static volatile bool s_radar_armed = false;
/* Core0 disconnect/ready ↔ Core1 input_process_task. false until device ready. */
static volatile bool s_connected = false;

/* play_dual_rumble()은 btstack 타이머 리스트를 조작하는데 그 리스트에는 락이 없다.
   Core1에서 직접 부르면 Core0 런루프의 타이머 순회와 경쟁해 리스트가 깨진다.
   따라서 Core1은 요청만 걸고 실제 호출은 btstack 스레드에서 수행한다. */
static btstack_context_callback_registration_t rumble_request;
static btstack_context_callback_registration_t gamepad_effect_request;
static uni_hid_device_t *volatile rumble_device = NULL;
static uint16_t volatile rumble_duration_ms = X_RUMBLE_DURATION_MS;
static uint8_t volatile rumble_weak = X_RUMBLE_WEAK;
static uint8_t volatile rumble_strong = X_RUMBLE_STRONG;
static uni_hid_device_t *volatile gamepad_effect_device = NULL;

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
    if (d != NULL && d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, rumble_duration_ms, rumble_weak, rumble_strong);
}

static void waiting_idle_cb(void *arg) {
    (void)arg;
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_IDLE);
}

/* 연결 효과음. btstack 스레드(on_device_ready)에서 UART를 쓰면 링크가 끊길 수 있어
   esp_timer 태스크에서 stop/play 한다. stop 직후 바로 play하면 DFPlayer가 무시하므로
   그 사이에 vTaskDelay를 둔다. */
static void connect_sound_cb(void *arg) {
    (void)arg;
    rccar_dfplayer_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_CONNECT);
}

/* 유예 후 스캔 재개. esp_timer 태스크에서 실행되므로 btstack 스레드에 위임하는
   _safe 변형을 쓴다 (_unsafe는 btstack 스레드 전용). */
static void scan_restart_cb(void *arg) {
    (void)arg;
    logi("custom: restarting scan\n");
    uni_bt_start_scanning_and_autoconnect_safe();
}

static void gamepad_effect_on_btstack_thread(void *context) {
    (void)context;
    uni_hid_device_t *d = gamepad_effect_device;
    if (d != NULL)
        trigger_event_on_gamepad(d);
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
    if (rccar_humidifier_available())
        rccar_humidifier_pulse_on_ms(HUMIDIFIER_PULSE_ON_MS);
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
    static bool select_start_fired = false;
    static uint16_t prev_buttons = 0;
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
            select_start_pressed_at = 0;
            select_start_fired = false;
            if (!failsafe_active) {
                failsafe_stop();
                failsafe_active = true;
            }
            continue;
        }

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

        /* X edge: rumble, then humidifier pulse (kingtiger1.1) */
        if (evt.buttons & BUTTON_X) {
            if (!(prev_buttons & BUTTON_X)) {
                request_rumble(evt.device, X_RUMBLE_DURATION_MS, X_RUMBLE_WEAK, X_RUMBLE_STRONG);
                if (rccar_humidifier_available()) {
                    esp_timer_stop(humidifier_pulse_timer);
                    esp_timer_start_once(humidifier_pulse_timer, (uint64_t)X_RUMBLE_DURATION_MS * 1000ULL);
                }
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
        }

        /* Select + Start hold: factory reset (NVS erase + reboot) */
        uint8_t sel = (evt.misc_buttons & MISC_BUTTON_SELECT) ? 1 : 0;
        uint8_t sta = (evt.misc_buttons & MISC_BUTTON_START) ? 1 : 0;
        if (sel && sta) {
            if (select_start_pressed_at == 0)
                select_start_pressed_at = now_ms;
            if (!select_start_fired && now_ms - select_start_pressed_at >= SELECT_START_HOLD_MS) {
                select_start_fired = true;
                request_rumble(evt.device, 800, 255, 255);
                esp_timer_stop(restart_timer);
                esp_timer_start_once(restart_timer, 800 * 1000);
            }
        } else {
            select_start_pressed_at = 0;
            select_start_fired = false;
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

    rumble_request.callback = &rumble_on_btstack_thread;
    rumble_request.context = NULL;
    gamepad_effect_request.callback = &gamepad_effect_on_btstack_thread;
    gamepad_effect_request.context = NULL;

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

    const esp_timer_create_args_t connect_sound_args = {
        .callback = &connect_sound_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "connect_sound",
    };
    esp_timer_create(&connect_sound_args, &connect_sound_timer);

    const esp_timer_create_args_t gamepad_effect_args = {
        .callback = &gamepad_effect_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gamepad_effect",
    };
    esp_timer_create(&gamepad_effect_args, &gamepad_effect_timer);

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
    /* 가상 자식 장치(DS4 터치패드 마우스 등)는 무시한다. 이 차는 게임패드만 쓴다. */
    if (d->parent != NULL) {
        logi("custom: ignoring virtual device\n");
        return;
    }
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
    /* Drop connection first so Core1 stops applying any late samples */
    s_connected = false;
    gamepad_effect_device = NULL;
    esp_timer_stop(gamepad_effect_timer);
    esp_timer_stop(humidifier_pulse_timer);
    failsafe_stop();
    if (input_queue != NULL)
        xQueueReset(input_queue);
    rccar_dfplayer_play(RCCAR_DFPLAYER_TRACK_IDLE);
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

    /* Ensure motors stopped / soft state consistent before accepting input */
    rccar_motor_all_stop();
    s_radar_armed = false;
    rccar_radar_set_armed(false);
    if (input_queue != NULL)
        xQueueReset(input_queue);

    esp_timer_stop(waiting_idle_timer);
    esp_timer_stop(connect_sound_timer);
    esp_timer_start_once(connect_sound_timer, 100 * 1000);

    /* 럼블/LED는 trigger_event_on_gamepad 한 번으로 끝낸다. DS4는 calibration/fw
       feature report 교환 중 출력 리포트를 받으면 링크를 끊을 수 있으므로 지연한다. */
    gamepad_effect_device = d;
    esp_timer_stop(gamepad_effect_timer);
    esp_timer_start_once(gamepad_effect_timer, GAMEPAD_EFFECT_DELAY_MS * 1000);

    s_connected = true;
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
        return;

    if (!s_connected || input_queue == NULL)
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
    // if (d->report_parser.play_dual_rumble != NULL)
    //     d->report_parser.play_dual_rumble(d, 0, 150, 128, 40);
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
