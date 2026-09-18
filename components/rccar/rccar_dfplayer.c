/**
 * @file rccar_dfplayer.c
 * @brief DFPlayer Mini UART 제어 (DFPlayerMini_Fast 프로토콜 기준)
 * @see https://github.com/PowerBroker2/DFPlayerMini_Fast
 *
 * TX 전용. RX 핀이 NC라 트랙 종료(0x3D)를 받지 못하므로,
 * 짧은 효과음은 타이머 후 IDLE 루프로 돌아간다.
 */
#include "rccar_dfplayer.h"
#include "rccar_pins.h"

#include <stdbool.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rccar_dfplayer";

#define UART_NUM      UART_NUM_1
#define UART_BAUD     9600
#define UART_BUF_SIZE 256

/* DFPlayerMini_Fast 패킷 상수 */
#define DFPLAYER_SB          0x7E
#define DFPLAYER_VER         0xFF
#define DFPLAYER_LEN         0x06
#define DFPLAYER_NO_FEEDBACK 0x00
#define DFPLAYER_EB          0xEF

/* 제어 명령 */
#define DFPLAYER_CMD_PLAY          0x03
#define DFPLAYER_CMD_VOLUME        0x06
#define DFPLAYER_CMD_PLAYBACK_MODE 0x08 /* 루프 재생 (param = 트랙 번호) */
#define DFPLAYER_CMD_STOP          0x16

#define DFPLAYER_STACK_SIZE 10

/** 전송 패킷 (stack). DFPlayerMini_Fast과 동일 레이아웃 */
typedef struct {
    uint8_t start_byte;
    uint8_t version;
    uint8_t length;
    uint8_t command_value;
    uint8_t feedback_value;
    uint8_t param_msb;
    uint8_t param_lsb;
    uint8_t checksum_msb;
    uint8_t checksum_lsb;
    uint8_t end_byte;
} dfplayer_stack_t;

static bool s_inited = false;
static esp_timer_handle_t s_resume_idle_timer = NULL;

/**
 * @brief 체크섬 계산 (DFPlayerMini_Fast findChecksum 동일)
 * checksum = (~(version + length + command + feedback + paramMSB + paramLSB)) + 1
 */
static void dfplayer_find_checksum(dfplayer_stack_t *stack)
{
    uint16_t sum = (uint16_t)(stack->version + stack->length + stack->command_value
                              + stack->feedback_value + stack->param_msb + stack->param_lsb);
    uint16_t checksum = (uint16_t)((~sum) + 1);
    stack->checksum_msb = (uint8_t)(checksum >> 8);
    stack->checksum_lsb = (uint8_t)(checksum & 0xFF);
}

/** 패킷 전송 */
static esp_err_t dfplayer_send_stack(const dfplayer_stack_t *stack)
{
    uint8_t buf[DFPLAYER_STACK_SIZE] = {
        stack->start_byte,
        stack->version,
        stack->length,
        stack->command_value,
        stack->feedback_value,
        stack->param_msb,
        stack->param_lsb,
        stack->checksum_msb,
        stack->checksum_lsb,
        stack->end_byte,
    };
    int n = uart_write_bytes(UART_NUM, buf, DFPLAYER_STACK_SIZE);
    if (n != DFPLAYER_STACK_SIZE) {
        ESP_LOGE(TAG, "uart_write_bytes %d", n);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/** 명령 1회 전송 (공통) */
static esp_err_t dfplayer_send_cmd(uint8_t cmd, uint8_t param_msb, uint8_t param_lsb)
{
    dfplayer_stack_t stack = {
        .start_byte = DFPLAYER_SB,
        .version = DFPLAYER_VER,
        .length = DFPLAYER_LEN,
        .command_value = cmd,
        .feedback_value = DFPLAYER_NO_FEEDBACK,
        .param_msb = param_msb,
        .param_lsb = param_lsb,
        .end_byte = DFPLAYER_EB,
    };
    dfplayer_find_checksum(&stack);
    return dfplayer_send_stack(&stack);
}

static void cancel_resume_idle(void)
{
    if (s_resume_idle_timer != NULL) {
        esp_timer_stop(s_resume_idle_timer);
    }
}

static void resume_idle_cb(void *arg)
{
    (void)arg;
    rccar_dfplayer_play_loop(RCCAR_DFPLAYER_TRACK_IDLE);
}

static void schedule_resume_idle(uint8_t track)
{
    uint32_t ms = rccar_dfplayer_resume_idle_ms(track);
    cancel_resume_idle();
    if (ms == 0 || s_resume_idle_timer == NULL) {
        return;
    }
    esp_timer_start_once(s_resume_idle_timer, (uint64_t)ms * 1000ULL);
}

esp_err_t rccar_dfplayer_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = uart_set_pin(UART_NUM, RCCAR_PIN_SOUND_TX, RCCAR_PIN_SOUND_RX, -1, -1);
    if (ret != ESP_OK) {
        return ret;
    }
    /* TX 전용. RX 태스크는 없지만 드라이버는 RX 링버퍼를 요구한다. */
    ret = uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    const esp_timer_create_args_t resume_args = {
        .callback = &resume_idle_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dfplayer_idle",
    };
    ret = esp_timer_create(&resume_args, &s_resume_idle_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    s_inited = true;
    ESP_LOGI(TAG, "dfplayer init ok (TX=GPIO%d, RX unused)", (int)RCCAR_PIN_SOUND_TX);
    return ESP_OK;
}

esp_err_t rccar_dfplayer_play(uint8_t track)
{
    if (track < 1) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGD(TAG, "play track %u", (unsigned)track);
    /* PLAY(0x03): param = track number */
    esp_err_t ret = dfplayer_send_cmd(DFPLAYER_CMD_PLAY, (uint8_t)((uint16_t)track >> 8),
                                      (uint8_t)(track & 0xFF));
    if (ret == ESP_OK) {
        schedule_resume_idle(track);
    }
    return ret;
}

esp_err_t rccar_dfplayer_play_loop(uint8_t track)
{
    if (track < 1) {
        return ESP_ERR_INVALID_ARG;
    }
    cancel_resume_idle();
    ESP_LOGD(TAG, "play_loop track %u", (unsigned)track);
    /* 0x08: Playback Mode (Single Track Loop) */
    return dfplayer_send_cmd(DFPLAYER_CMD_PLAYBACK_MODE, (uint8_t)((uint16_t)track >> 8),
                             (uint8_t)(track & 0xFF));
}

esp_err_t rccar_dfplayer_set_volume(uint8_t vol)
{
    if (vol > 30) {
        vol = 30;
    }
    /* VOLUME(0x06): paramLSB = 0~30 */
    return dfplayer_send_cmd(DFPLAYER_CMD_VOLUME, 0x00, vol);
}

esp_err_t rccar_dfplayer_stop(void)
{
    cancel_resume_idle();
    return dfplayer_send_cmd(DFPLAYER_CMD_STOP, 0x00, 0x00);
}
