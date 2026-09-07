/**
 * @file rccar_neopixel.h
 * @brief WS2812 네오픽셀 (GPIO13, 8개)
 */
#ifndef RCCAR_NEOPIXEL_H
#define RCCAR_NEOPIXEL_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_neopixel_init(void);

/** @brief 엔진 효과 ON/OFF 토글 */
void rccar_neopixel_toggle(void);

/** @brief 엔진 효과 켜기/끄기. 가습기 미스트 중이면 종료 후 반영 */
void rccar_neopixel_set_enabled(bool enabled);

bool rccar_neopixel_is_enabled(void);

/**
 * @brief 가습기 ON 시 옅은 주황을 오래 유지한 뒤 흰색으로 전환 + 엔진형 떨림.
 * OFF 시 미스트를 끝내고 엔진 효과 설정으로 복귀한다.
 */
void rccar_neopixel_mist_set(bool on);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_NEOPIXEL_H */
