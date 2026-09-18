/**
 * @file rccar_dfplayer.h
 * @brief DFPlayer Mini UART 제어 (효과음)
 */
#ifndef RCCAR_DFPLAYER_H
#define RCCAR_DFPLAYER_H

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
 * TX 전용이라 트랙 종료(0x3D)를 받지 못한다.
 * 짧은 효과음만 이 시간(ms) 뒤에 IDLE 루프로 돌아간다. 0이면 재개하지 않는다.
 */
static inline uint32_t rccar_dfplayer_resume_idle_ms(uint8_t track)
{
    switch (track) {
    case RCCAR_DFPLAYER_TRACK_GUN:
        return 2500;
    case RCCAR_DFPLAYER_TRACK_MG:
        return 1500;
    case RCCAR_DFPLAYER_TRACK_CONNECT:
        return 4000;
    case RCCAR_DFPLAYER_TRACK_NITRO:
        return 3000;
    default:
        return 0;
    }
}

/** 볼륨 설정 (0~30) */
esp_err_t rccar_dfplayer_set_volume(uint8_t vol);

/** 재생 중지 */
esp_err_t rccar_dfplayer_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_DFPLAYER_H */
