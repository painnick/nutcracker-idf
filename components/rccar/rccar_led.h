/**
 * @file rccar_led.h
 * @brief 웜 화이트 MOSFET + 595 테스트 패턴
 */
#ifndef RCCAR_LED_H
#define RCCAR_LED_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 웜 화이트 GPIO 초기화 (OFF)
 * @return ESP_OK on success
 */
esp_err_t rccar_led_init(void);

/**
 * @brief 웜 화이트 ON/OFF
 */
void rccar_led_warm_white_set(bool on);

/**
 * @brief 웜 화이트 토글 (내부 상태 유지)
 */
void rccar_led_warm_white_toggle(void);

/**
 * @brief 595 체이스 테스트 1스텝 (단일 비트 이동)
 */
void rccar_led_test_chase_step(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_LED_H */
