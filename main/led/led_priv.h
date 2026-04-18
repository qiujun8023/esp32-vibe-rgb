/* 仅供 led 模块内部使用,外部请用 led.h */

#pragma once

#include <led_strip.h>
#include <stdint.h>

#include "led.h"

/* 定义于 led_map.c */
extern int     s_w, s_h;
extern uint8_t s_serpentine, s_start, s_rotation;
extern int16_t s_lookup[LED_MAX_COUNT];

/* 定义于 led_fb.c */
extern uint8_t s_fb[LED_MAX_COUNT * 3];
extern uint8_t s_brightness;

/* 定义于 led_hw.c */
extern led_strip_handle_t s_strip;

void led_map_rebuild(void);