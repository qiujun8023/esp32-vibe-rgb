#pragma once
// LED 模块内部共享头，仅供 led_map.c / led_fb.c / led_hw.c 使用，禁止外部引用

#include <stdint.h>
#include <led_strip.h>

#include "led.h"

// ── 定义于 led_map.c ──────────────────────────────────────────────────────────
extern int     s_w, s_h;
extern uint8_t s_serpentine, s_start, s_rotation;
extern int16_t s_lookup[LED_MAX_COUNT];

// ── 定义于 led_fb.c ───────────────────────────────────────────────────────────
extern uint8_t s_fb[LED_MAX_COUNT * 3];
extern uint8_t s_brightness;

// ── 定义于 led_hw.c ───────────────────────────────────────────────────────────
extern led_strip_handle_t s_strip;

// ── 内部函数 ──────────────────────────────────────────────────────────────────
// 根据当前 s_rotation/s_serpentine/s_start 重建 s_lookup 查找表
void ledmap_rebuild(void);
