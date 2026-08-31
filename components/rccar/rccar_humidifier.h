/**
 * @file rccar_humidifier.h
 * @brief 가습기 모듈 GPIO (GPIO4, HIGH=ON)
 */
#ifndef RCCAR_HUMIDIFIER_H
#define RCCAR_HUMIDIFIER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_humidifier_init(void);

/** @brief 즉시 ON/OFF (HIGH=ON, LOW=OFF) */
void rccar_humidifier_set(bool on);

/**
 * @brief on_ms 동안 ON 후 기본 상태(OFF)로 복귀
 * @note 이미 ON 중이면 타이머를 재시작한다.
 */
void rccar_humidifier_pulse_on_ms(uint32_t on_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_HUMIDIFIER_H */
