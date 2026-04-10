/**
 * @file led.h
 * @brief LED 矩阵控制接口
 */

#pragma once

#include <stdint.h>

#include "settings.h"

#define LED_MAX_COUNT 256

/**
 * @brief 初始化 LED 硬件
 */
void led_init(const settings_t* s);

/**
 * @brief 应用新设置
 */
void led_apply_settings(const settings_t* s);

/**
 * @brief 设置像素颜色（RGB）
 */
void led_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 设置像素颜色（HSV）
 */
void led_set_pixel_hsv(int x, int y, uint16_t h, uint8_t s, uint8_t v);

/**
 * @brief 读取像素颜色
 */
void led_get_pixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b);

/**
 * @brief 填充所有像素
 */
void led_fill(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 清空所有像素
 */
void led_clear(void);

/**
 * @brief 刷新显示
 */
void led_flush(void);

/**
 * @brief 按索引设置像素
 */
void led_set_pixel_idx(int idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 测试单个 LED（物理索引）
 */
void led_hw_test_pixel(int idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 所有像素淡出
 */
void led_fade_all(uint8_t rate);

/**
 * @brief 2D 模糊
 */
void led_blur2d(uint8_t amount);

/**
 * @brief 导出帧缓冲
 */
void led_get_fb(uint8_t* buf, int* len);

/**
 * @brief 获取矩阵宽度
 */
int led_width(void);

/**
 * @brief 获取矩阵高度
 */
int led_height(void);

/**
 * @brief 获取像素总数
 */
int led_count(void);