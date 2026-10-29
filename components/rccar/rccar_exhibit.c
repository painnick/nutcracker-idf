/**
 * @file rccar_exhibit.c
 * @brief 패드와 보드가 없을 때 테이블 전시. 휠은 항상 정지.
 *
 * 1. 포탑 2초, 1초 뒤 레이저 두 번, 3초 대기
 * 2. 1초 뒤 헤드라이트 3초 후 끄기, 1초 대기
 * 3. 포탑 2초 후 정지. 발사 없음. 방향은 1번과 반대
 * 4. 1초 뒤 헤드라이트 3초 후 끄기, 1초 대기
 * 5. 포탑 2초, 1초 뒤 개틀링, 1초 뒤 개틀링, 3초 대기. 방향은 3번과 반대
 * 6. 가습기와 네오픽셀 2초, 3초 대기
 * 7. 0006~0009 중 하나. 재생되는 동안 레이더 왕복. 끝나면 레이더를 끄고 2초 대기
 * 8. 1번부터 반복
 *
 * 회전마다 직전과 반대이다. 새 begin 의 첫 회전만 오른쪽이다.
 *
 * 재생 길이는 sdcard/mp3 를 읽어 rccar_dfplayer_resume_idle_ms() 에 넣었다.
 */
#include "rccar_exhibit.h"

#include "rccar_dfplayer.h"
#include "rccar_headlight.h"
#include "rccar_humidifier.h"
#include "rccar_laser.h"
#include "rccar_motor.h"
#include "rccar_radar.h"

#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "exhibit";

#define EXHIBIT_TURRET_SPEED 384
#define EXHIBIT_TURRET_MS 2000
#define EXHIBIT_ARM_MS 1000
#define EXHIBIT_LASER_SOUND_MS 200
#define EXHIBIT_AFTER_LASER_MS 3000
#define EXHIBIT_HEADLIGHT_DELAY_MS 1000
#define EXHIBIT_HEADLIGHT_MS 3000
#define EXHIBIT_AFTER_HEADLIGHT_MS 1000
#define EXHIBIT_AFTER_GATLING_MS 3000
#define EXHIBIT_MIST_MS 2000
#define EXHIBIT_AFTER_MIST_MS 3000
#define EXHIBIT_AFTER_SFX_MS 2000

enum {
    EXHIBIT_START = 0,
    EXHIBIT_TURRET_A,
    EXHIBIT_LASER_ARM,
    EXHIBIT_LASER_GAP,
    EXHIBIT_LASER_HOLD,
    EXHIBIT_HEADLIGHT_DELAY,
    EXHIBIT_HEADLIGHT,
    EXHIBIT_AFTER_HEADLIGHT,
    EXHIBIT_TURRET_REV,
    EXHIBIT_HEADLIGHT_DELAY_2,
    EXHIBIT_HEADLIGHT_2,
    EXHIBIT_AFTER_HEADLIGHT_2,
    EXHIBIT_TURRET_B,
    EXHIBIT_GATLING_ARM,
    EXHIBIT_GATLING_GAP,
    EXHIBIT_AFTER_GATLING,
    EXHIBIT_MIST,
    EXHIBIT_AFTER_MIST,
    EXHIBIT_SFX,
    EXHIBIT_AFTER_SFX,
};

static bool s_active = false;
static int s_phase = EXHIBIT_START;
static int64_t s_phase_until_ms = 0;
static int s_turret_cmd = 0;
static int s_turret_sign = 1;
static bool s_gun_pending = false;
static int64_t s_gun_due_ms = 0;

static bool output_blocked(rccar_exhibit_is_connected_fn is_connected)
{
    return is_connected != NULL && is_connected();
}

static void enter_phase(int phase, int64_t now_ms, int64_t duration_ms)
{
    s_phase = phase;
    s_phase_until_ms = now_ms + duration_ms;
}

static void arm_headlight(int64_t now_ms, int phase)
{
    enter_phase(phase, now_ms, EXHIBIT_HEADLIGHT_DELAY_MS);
    ESP_LOGI(TAG, "headlight delay %d ms", EXHIBIT_HEADLIGHT_DELAY_MS);
}

static void start_headlight(int64_t now_ms, int phase)
{
    rccar_headlight_set(true);
    enter_phase(phase, now_ms, EXHIBIT_HEADLIGHT_MS);
    ESP_LOGI(TAG, "headlight %d ms", EXHIBIT_HEADLIGHT_MS);
}

static void start_turret(int64_t now_ms, int phase)
{
    s_turret_cmd = s_turret_sign * EXHIBIT_TURRET_SPEED;
    s_turret_sign = -s_turret_sign;
    rccar_motor_turret_set(s_turret_cmd);
    enter_phase(phase, now_ms, EXHIBIT_TURRET_MS);
    ESP_LOGI(TAG, "turret %d ms dir %d", EXHIBIT_TURRET_MS, s_turret_cmd > 0 ? 1 : -1);
}

static void play_track(uint8_t track)
{
    rccar_dfplayer_play(track);
    ESP_LOGI(TAG, "sound %u", (unsigned)track);
}

static void arm_laser_shot(int64_t now_ms)
{
    rccar_laser_fire();
    s_gun_pending = true;
    s_gun_due_ms = now_ms + EXHIBIT_LASER_SOUND_MS;
}

static int64_t laser_repeat_gap_ms(void)
{
    return (int64_t)EXHIBIT_LASER_SOUND_MS +
           (int64_t)rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_GUN);
}

static void fire_laser(int64_t now_ms)
{
    int64_t gap_ms = laser_repeat_gap_ms();
    arm_laser_shot(now_ms);
    enter_phase(EXHIBIT_LASER_GAP, now_ms, gap_ms);
    ESP_LOGI(TAG, "fire laser 1, next in %lld ms", (long long)gap_ms);
}

static void fire_gatling_once(void)
{
    play_track(RCCAR_DFPLAYER_TRACK_MG);
    rccar_laser_gatling();
}

static void fire_gatling(int64_t now_ms)
{
    fire_gatling_once();
    enter_phase(EXHIBIT_GATLING_GAP, now_ms, EXHIBIT_ARM_MS);
    ESP_LOGI(TAG, "fire gatling 1, next in %d ms", EXHIBIT_ARM_MS);
}

static void play_cycle_sfx(int64_t now_ms)
{
    static const uint8_t tracks[4] = {
        RCCAR_DFPLAYER_TRACK_BGM_MIN,
        (uint8_t)(RCCAR_DFPLAYER_TRACK_BGM_MIN + 1),
        (uint8_t)(RCCAR_DFPLAYER_TRACK_BGM_MIN + 2),
        RCCAR_DFPLAYER_TRACK_BGM_MAX,
    };
    uint8_t track = tracks[esp_random() % 4];
    uint32_t play_ms = rccar_dfplayer_resume_idle_ms(track);
    play_track(track);
    rccar_radar_exhibit_begin();
    enter_phase(EXHIBIT_SFX, now_ms, (int64_t)play_ms);
    ESP_LOGI(TAG, "cycle sfx %u for %u ms, radar on", (unsigned)track, (unsigned)play_ms);
}

void rccar_exhibit_begin(int64_t now_ms)
{
    (void)now_ms;
    s_active = true;
    s_phase = EXHIBIT_START;
    s_phase_until_ms = 0;
    s_turret_cmd = 0;
    s_turret_sign = 1;
    s_gun_pending = false;
    s_gun_due_ms = 0;
    rccar_headlight_set(false);
    ESP_LOGI(TAG, "begin");
}

void rccar_exhibit_abort(void)
{
    if (!s_active) {
        return;
    }
    s_active = false;
    s_phase = EXHIBIT_START;
    s_turret_cmd = 0;
    s_gun_pending = false;
    rccar_motor_turret_set(0);
    rccar_headlight_set(false);
    rccar_radar_set_enabled(false);
    rccar_humidifier_set(false);
    rccar_laser_stop();
    rccar_motor_wheel_set(0, 0, 0, 0);
    ESP_LOGI(TAG, "abort");
}

void rccar_exhibit_tick(int64_t now_ms, rccar_exhibit_is_connected_fn is_connected)
{
    if (!s_active || output_blocked(is_connected)) {
        return;
    }

    rccar_motor_wheel_set(0, 0, 0, 0);

    if ((s_phase == EXHIBIT_LASER_GAP || s_phase == EXHIBIT_LASER_HOLD) && s_gun_pending &&
        now_ms >= s_gun_due_ms) {
        if (output_blocked(is_connected)) {
            return;
        }
        play_track(RCCAR_DFPLAYER_TRACK_GUN);
        s_gun_pending = false;
    }

    if (s_phase == EXHIBIT_TURRET_A || s_phase == EXHIBIT_TURRET_REV || s_phase == EXHIBIT_TURRET_B) {
        if (now_ms < s_phase_until_ms) {
            if (!output_blocked(is_connected)) {
                rccar_motor_turret_set(s_turret_cmd);
            }
            return;
        }
        if (output_blocked(is_connected)) {
            return;
        }
        s_turret_cmd = 0;
        rccar_motor_turret_set(0);
        if (s_phase == EXHIBIT_TURRET_A) {
            enter_phase(EXHIBIT_LASER_ARM, now_ms, EXHIBIT_ARM_MS);
            ESP_LOGI(TAG, "laser arm %d ms", EXHIBIT_ARM_MS);
        } else if (s_phase == EXHIBIT_TURRET_REV) {
            arm_headlight(now_ms, EXHIBIT_HEADLIGHT_DELAY_2);
        } else {
            enter_phase(EXHIBIT_GATLING_ARM, now_ms, EXHIBIT_ARM_MS);
            ESP_LOGI(TAG, "gatling arm %d ms", EXHIBIT_ARM_MS);
        }
        return;
    }

    if (s_phase != EXHIBIT_START && now_ms < s_phase_until_ms) {
        return;
    }
    if (output_blocked(is_connected)) {
        return;
    }

    switch (s_phase) {
    case EXHIBIT_START:
        start_turret(now_ms, EXHIBIT_TURRET_A);
        break;
    case EXHIBIT_LASER_ARM:
        fire_laser(now_ms);
        break;
    case EXHIBIT_LASER_GAP:
        arm_laser_shot(now_ms);
        enter_phase(EXHIBIT_LASER_HOLD, now_ms, EXHIBIT_AFTER_LASER_MS);
        ESP_LOGI(TAG, "fire laser 2, wait %d ms", EXHIBIT_AFTER_LASER_MS);
        break;
    case EXHIBIT_LASER_HOLD:
        arm_headlight(now_ms, EXHIBIT_HEADLIGHT_DELAY);
        break;
    case EXHIBIT_HEADLIGHT_DELAY:
        start_headlight(now_ms, EXHIBIT_HEADLIGHT);
        break;
    case EXHIBIT_HEADLIGHT:
        rccar_headlight_set(false);
        enter_phase(EXHIBIT_AFTER_HEADLIGHT, now_ms, EXHIBIT_AFTER_HEADLIGHT_MS);
        ESP_LOGI(TAG, "headlight off, wait %d ms", EXHIBIT_AFTER_HEADLIGHT_MS);
        break;
    case EXHIBIT_AFTER_HEADLIGHT:
        start_turret(now_ms, EXHIBIT_TURRET_REV);
        break;
    case EXHIBIT_HEADLIGHT_DELAY_2:
        start_headlight(now_ms, EXHIBIT_HEADLIGHT_2);
        break;
    case EXHIBIT_HEADLIGHT_2:
        rccar_headlight_set(false);
        enter_phase(EXHIBIT_AFTER_HEADLIGHT_2, now_ms, EXHIBIT_AFTER_HEADLIGHT_MS);
        ESP_LOGI(TAG, "headlight off, wait %d ms", EXHIBIT_AFTER_HEADLIGHT_MS);
        break;
    case EXHIBIT_AFTER_HEADLIGHT_2:
        start_turret(now_ms, EXHIBIT_TURRET_B);
        break;
    case EXHIBIT_GATLING_ARM:
        fire_gatling(now_ms);
        break;
    case EXHIBIT_GATLING_GAP:
        fire_gatling_once();
        enter_phase(EXHIBIT_AFTER_GATLING, now_ms, EXHIBIT_AFTER_GATLING_MS);
        ESP_LOGI(TAG, "fire gatling 2, wait %d ms", EXHIBIT_AFTER_GATLING_MS);
        break;
    case EXHIBIT_AFTER_GATLING:
        rccar_humidifier_set(true);
        enter_phase(EXHIBIT_MIST, now_ms, EXHIBIT_MIST_MS);
        ESP_LOGI(TAG, "mist %d ms", EXHIBIT_MIST_MS);
        break;
    case EXHIBIT_MIST:
        rccar_humidifier_set(false);
        enter_phase(EXHIBIT_AFTER_MIST, now_ms, EXHIBIT_AFTER_MIST_MS);
        ESP_LOGI(TAG, "mist off, wait %d ms", EXHIBIT_AFTER_MIST_MS);
        break;
    case EXHIBIT_AFTER_MIST:
        play_cycle_sfx(now_ms);
        break;
    case EXHIBIT_SFX:
        rccar_radar_set_enabled(false);
        enter_phase(EXHIBIT_AFTER_SFX, now_ms, EXHIBIT_AFTER_SFX_MS);
        ESP_LOGI(TAG, "sfx done, radar off, wait %d ms", EXHIBIT_AFTER_SFX_MS);
        break;
    case EXHIBIT_AFTER_SFX:
        start_turret(now_ms, EXHIBIT_TURRET_A);
        break;
    default:
        break;
    }
}
