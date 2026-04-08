#pragma once

#include <stdint.h>

#include "settings.h"

#define LED_MAX_COUNT 256

/**
 * 初始化 LED 驱动
 */
void led_init(const settings_t* s);

/**
 * 运行时更新 LED 设置
 */
void led_apply_settings(const settings_t* s);

/**
 * 设置指定逻辑坐标的像素颜色
 * 坐标系：左下角为 (0,0)
 */
void led_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);

/**
 * 使用 HSV 颜色设置像素
 */
void led_set_pixel_hsv(int x, int y, uint16_t h, uint8_t s, uint8_t v);

/**
 * 获取指定逻辑坐标的当前颜色
 */
void led_get_pixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b);

/**
 * 使用指定颜色填充整个矩阵
 */
void led_fill(uint8_t r, uint8_t g, uint8_t b);

/**
 * 清空显示内容
 */
void led_clear(void);

/**
 * 提交帧缓冲到硬件刷新
 */
void led_flush(void);

/**
 * 按物理索引直接设置像素（用于布线测试）
 */
void led_set_pixel_idx(int idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * 全屏色彩消退效果
 */
void led_fade_all(uint8_t rate);

/**
 * 二维空间模糊滤镜
 */
void led_blur2d(uint8_t amount);

/**
 * 获取当前逻辑帧缓冲数据（用于 Web 预览）
 */
void led_get_fb(uint8_t* buf, int* len);

/**
 * 获取逻辑矩阵宽度
 */
int led_width(void);

/**
 * 获取逻辑矩阵高度
 */
int led_height(void);

/**
 * 获取矩阵总像素点数
 */
int led_count(void);