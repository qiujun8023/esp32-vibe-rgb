/**
 * @file led_priv.h
 * @brief LED 模块内部共享头，仅供 led_map.c、led_fb.c、led_hw.c 使用
 *
 * 外部模块请使用 led.h 公开接口
 */

#pragma once

#include <led_strip.h>
#include <stdint.h>

#include "led.h"

/* 声明于 led_map.c */
extern int     s_w, s_h;
extern uint8_t s_serpentine, s_start, s_rotation;
extern int16_t s_lookup[LED_MAX_COUNT];

/* 声明于 led_fb.c */
extern uint8_t s_fb[LED_MAX_COUNT * 3];
extern uint8_t s_brightness;

/* 声明于 led_hw.c */
extern led_strip_handle_t s_strip;

/**
 * @brief 重建坐标查找表
 *
 * 根据当前 rotation、serpentine、start 参数重建 s_lookup
 */
void ledmap_rebuild(void);