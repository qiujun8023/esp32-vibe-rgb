#pragma once
#include <stdint.h>

#include "settings.h"

#define LED_MAX_COUNT 256  // 静态分配上限

void led_init(const settings_t* s);
void led_apply_settings(const settings_t* s);  // 仅更新亮度（不重初始化硬件）

// 基本绘制（坐标系：x=列 y=行，原点 = led_start 角）
void led_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void led_set_pixel_hsv(int x, int y, uint16_t h, uint8_t s, uint8_t v);
void led_get_pixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b);
void led_fill(uint8_t r, uint8_t g, uint8_t b);
void led_clear(void);
void led_flush(void);

// 直接按索引设置（用于测试，绕过坐标转换）
void led_set_pixel_idx(int idx, uint8_t r, uint8_t g, uint8_t b);

// 效果辅助
void led_fade_all(uint8_t rate);  // 所有像素向黑衰减
void led_blur2d(uint8_t amount);  // 2D 模糊（amount=0-255）

// 帧缓冲访问（用于 WebSocket 推送实际 LED 状态）
void led_get_fb(uint8_t* buf, int* len);  // 写入 RGB 平铺，len=像素数*3

// 矩阵尺寸（初始化后从 settings 读取）
int led_width(void);
int led_height(void);
int led_count(void);
