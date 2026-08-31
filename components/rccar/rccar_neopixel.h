/**
 * @file rccar_neopixel.h
 * @brief WS2812 네오픽셀 (GPIO13, 4개)
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

/** @brief 효과 끄고 LED 소등 */
void rccar_neopixel_set_enabled(bool enabled);

bool rccar_neopixel_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_NEOPIXEL_H */
