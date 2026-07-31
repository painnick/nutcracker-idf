/**
 * @file rccar_led.h
 * @brief 웜 화이트 + 테스트 패턴 (스텁)
 */
#ifndef RCCAR_LED_H
#define RCCAR_LED_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_led_init(void);
void rccar_led_warm_white_set(bool on);
void rccar_led_warm_white_toggle(void);
void rccar_led_test_chase_step(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_LED_H */
