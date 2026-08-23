// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Ricardo Quesada
// http://retro.moe/unijoysticle2

#ifndef UNI_HID_PARSER_ANDROID_H
#define UNI_HID_PARSER_ANDROID_H

#include <stdint.h>

#include "parser/uni_hid_parser.h"

// Android devices
void uni_hid_parser_android_init_report(struct uni_hid_device_s* d);
void uni_hid_parser_android_parse_usage(struct uni_hid_device_s* d,
                                        const hid_globals_t* globals,
                                        uint16_t usage_page,
                                        uint16_t usage,
                                        int32_t value);
void uni_hid_parser_android_set_player_leds(struct uni_hid_device_s* d, uint8_t leds);
void uni_hid_parser_android_play_dual_rumble(struct uni_hid_device_s* d,
                                             uint16_t start_delay_ms,
                                             uint16_t duration_ms,
                                             uint8_t weak_magnitude,
                                             uint8_t strong_magnitude);

#endif  // UNI_HID_PARSER_ANDROID_H
