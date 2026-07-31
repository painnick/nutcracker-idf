/**
 * @file rccar_shiftreg.h
 * @brief 74HC595×4 데이지체인 (32비트 비트뱅)
 *
 * bit0 = SER 직결 첫 칩 쪽, bit31 = 체인 끝 (MSB first 시프트).
 */
#ifndef RCCAR_SHIFTREG_H
#define RCCAR_SHIFTREG_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DATA/CLOCK/LATCH 출력 설정, 초기 패턴 0 래치
 * @return ESP_OK on success
 */
esp_err_t rccar_shiftreg_init(void);

/**
 * @brief 32비트 패턴을 595 체인에 시프트 후 래치 (MSB first)
 * @param bits bit0 = 체인 시작(SER 쪽), bit31 = 체인 끝
 */
void rccar_shiftreg_write32(uint32_t bits);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_SHIFTREG_H */
