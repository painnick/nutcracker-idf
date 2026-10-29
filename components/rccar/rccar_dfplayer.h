/**
 * @file rccar_dfplayer.h
 * @brief DFPlayer Mini UART 제어 (효과음)
 */
#ifndef RCCAR_DFPLAYER_H
#define RCCAR_DFPLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RCCAR_DFPLAYER_TRACK_IDLE    1 /* 0001.mp3 대기 반복 */
#define RCCAR_DFPLAYER_TRACK_GUN     2 /* 0002.mp3 포 발사 */
#define RCCAR_DFPLAYER_TRACK_MG      3 /* 0003.mp3 개틀링 */
#define RCCAR_DFPLAYER_TRACK_CONNECT 4 /* 0004.mp3 게임패드 연결 */
#define RCCAR_DFPLAYER_TRACK_NITRO   5 /* 0005.mp3 니트로 액션 */
#define RCCAR_DFPLAYER_TRACK_BGM_MIN 6 /* 0006.mp3 ~ 0009.mp3 연결 중 대기 BGM */
#define RCCAR_DFPLAYER_TRACK_BGM_MAX 9

esp_err_t rccar_dfplayer_init(void);

/** 트랙 재생 (1회) */
esp_err_t rccar_dfplayer_play(uint8_t track);

/** 트랙 반복 재생 */
esp_err_t rccar_dfplayer_play_loop(uint8_t track);

/**
 * sdcard/mp3 프레임을 읽어 얻은 재생 길이(ms).
 * 0002 Cannon 1907, 0003 Gatling 684, 0004 Tank Gear Shift 1097, 0005 Boost 2064.
 * 0006 War Horn And Drums 18155, 0007 War Drum Loop 7080,
 * 0008 Radio Communication 37512, 0009 War Horn 33960.
 * TX 전용이라 곡 끝(0x3D)은 받지 못한다. 0이면 재개하지 않는다.
 * 기기가 연결된 동안(`rccar_dfplayer_set_waiting_idle(false)`)은 재개하지 않는다.
 */
static inline uint32_t rccar_dfplayer_resume_idle_ms(uint8_t track)
{
    switch (track) {
    case RCCAR_DFPLAYER_TRACK_GUN:
        return 1907;
    case RCCAR_DFPLAYER_TRACK_MG:
        return 684;
    case RCCAR_DFPLAYER_TRACK_CONNECT:
        return 1097;
    case RCCAR_DFPLAYER_TRACK_NITRO:
        return 2064;
    case RCCAR_DFPLAYER_TRACK_BGM_MIN:
        return 18155;
    case RCCAR_DFPLAYER_TRACK_BGM_MIN + 1:
        return 7080;
    case RCCAR_DFPLAYER_TRACK_BGM_MIN + 2:
        return 37512;
    case RCCAR_DFPLAYER_TRACK_BGM_MAX:
        return 33960;
    default:
        return 0;
    }
}

/** 볼륨 설정 (0~30) */
esp_err_t rccar_dfplayer_set_volume(uint8_t vol);

/** 재생 중지 */
esp_err_t rccar_dfplayer_stop(void);

/** 대기음(0001) 자동 재개 허용. 기기 연결 중에는 false. */
void rccar_dfplayer_set_waiting_idle(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_DFPLAYER_H */
