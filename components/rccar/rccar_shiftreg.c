/**
 * @file rccar_shiftreg.c
 * @brief 74HC595 x4 (스텁)
 */
#include "rccar_shiftreg.h"

esp_err_t rccar_shiftreg_init(void)
{
    return ESP_OK;
}

void rccar_shiftreg_write32(uint32_t bits)
{
    (void)bits;
}
