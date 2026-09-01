/**
 * @file rccar_headlight.h
 * @brief 헤드라이트 LED GPIO (GPIO14, HIGH=ON)
 */
#ifndef RCCAR_HEADLIGHT_H
#define RCCAR_HEADLIGHT_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_headlight_init(void);

void rccar_headlight_toggle(void);
void rccar_headlight_set(bool on);
bool rccar_headlight_is_on(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_HEADLIGHT_H */
