/**
 * @file rccar_shiftreg.h
 * @brief 74HC595 x4 (스텁)
 */
#ifndef RCCAR_SHIFTREG_H
#define RCCAR_SHIFTREG_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_shiftreg_init(void);
void rccar_shiftreg_write32(uint32_t bits);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_SHIFTREG_H */
