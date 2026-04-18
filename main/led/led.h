#pragma once

#include <stdint.h>

#include "settings.h"

#define LED_MAX_COUNT 256

void led_init(const settings_t* s);
void led_apply_settings(const settings_t* s);

void led_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void led_set_pixel_hsv(int x, int y, uint16_t h, uint8_t s, uint8_t v);
void led_get_pixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b);
void led_fill(uint8_t r, uint8_t g, uint8_t b);
void led_clear(void);
void led_flush(void);

void led_set_pixel_idx(int idx, uint8_t r, uint8_t g, uint8_t b);

/* 物理索引,不走 rotation/serpentine 映射 */
void led_hw_test_pixel(int idx, uint8_t r, uint8_t g, uint8_t b);

void led_fade_all(uint8_t rate);
void led_blur2d(uint8_t amount);
void led_get_fb(uint8_t* buf, int* len);

int led_width(void);
int led_height(void);
int led_count(void);
