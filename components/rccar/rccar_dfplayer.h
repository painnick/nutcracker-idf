/**
 * @file rccar_dfplayer.h
 * @brief DFPlayer 사운드 (스텁)
 */
#ifndef RCCAR_DFPLAYER_H
#define RCCAR_DFPLAYER_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_dfplayer_init(void);
esp_err_t rccar_dfplayer_play(uint8_t track);
esp_err_t rccar_dfplayer_set_volume(uint8_t vol);
esp_err_t rccar_dfplayer_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_DFPLAYER_H */
