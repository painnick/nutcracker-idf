/**
 * @file rccar_shiftreg.c
 * @brief 74HC595×4 데이지체인 비트뱅 (32비트)
 *
 * 시프트: MSB first (bit31 → 먼저 클록 아웃).
 * 매핑: bit0 = SER 직결 첫 칩 쪽(체인 시작), bit31 = 체인 끝 칩.
 * OE=GND(상시 enable), MR=VCC(하드웨어 클리어 없음) — 설계 3.1.
 */
#include "rccar_shiftreg.h"
#include "rccar_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "rccar_shiftreg";

static bool s_inited = false;

void rccar_shiftreg_write32(uint32_t bits)
{
    if (!s_inited) {
        return;
    }

    for (int i = 31; i >= 0; --i) {
        gpio_set_level(RCCAR_PIN_595_DATA, (int)((bits >> i) & 1u));
        gpio_set_level(RCCAR_PIN_595_CLOCK, 1);
        gpio_set_level(RCCAR_PIN_595_CLOCK, 0);
    }
    gpio_set_level(RCCAR_PIN_595_LATCH, 1);
    gpio_set_level(RCCAR_PIN_595_LATCH, 0);
}

esp_err_t rccar_shiftreg_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RCCAR_PIN_595_DATA) | (1ULL << RCCAR_PIN_595_CLOCK) |
                        (1ULL << RCCAR_PIN_595_LATCH),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(RCCAR_PIN_595_DATA, 0);
    gpio_set_level(RCCAR_PIN_595_CLOCK, 0);
    gpio_set_level(RCCAR_PIN_595_LATCH, 0);

    s_inited = true;
    rccar_shiftreg_write32(0);

    ESP_LOGI(TAG, "595 init ok (DATA=%d CLK=%d LATCH=%d)", (int)RCCAR_PIN_595_DATA,
             (int)RCCAR_PIN_595_CLOCK, (int)RCCAR_PIN_595_LATCH);
    return ESP_OK;
}
