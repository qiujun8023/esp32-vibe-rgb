/**
 * @file palettes.h
 * @brief 调色板定义与颜色工具函数
 */

#pragma once

#include <math.h>
#include <stdint.h>

/**
 * @brief RGB 颜色结构
 */
typedef struct {
    uint8_t r, g, b;
} rgb_t;

#define PALETTE_COUNT 12

#define PALETTE_RAINBOW 0
#define PALETTE_PARTY   1
#define PALETTE_SUNSET  2
#define PALETTE_LAVA    3
#define PALETTE_HEAT    4
#define PALETTE_SAKURA  5
#define PALETTE_OCEAN   6
#define PALETTE_AURORA  7
#define PALETTE_FOREST  8
#define PALETTE_CYBER   9
#define PALETTE_MONO    10
#define PALETTE_RANDOM  11

static const char* const PALETTE_NAMES[PALETTE_COUNT] = {"彩虹", "派对", "日落", "岩浆", "热力", "梦幻",
                                                         "海洋", "极光", "森林", "赛博", "单色", "随机"};

/**
 * @brief 线性插值
 */
static inline rgb_t _lerp_rgb(rgb_t a, rgb_t b, uint8_t t) {
    return (rgb_t){
        .r = (uint8_t)(a.r + (int)(b.r - a.r) * t / 255),
        .g = (uint8_t)(a.g + (int)(b.g - a.g) * t / 255),
        .b = (uint8_t)(a.b + (int)(b.b - a.b) * t / 255),
    };
}

/**
 * @brief 调色板颜色停靠点插值
 */
static inline rgb_t _stops(const rgb_t* stops, int n, uint8_t pos) {
    if (n <= 1) return stops[0];
    int seg = (int)pos * (n - 1) / 255;
    if (seg >= n - 1) return stops[n - 1];
    int rem = (int)pos * (n - 1) - seg * 255;
    return _lerp_rgb(stops[seg], stops[seg + 1], (uint8_t)rem);
}

/**
 * @brief HSV 转 RGB
 */
static inline rgb_t _hsv2rgb(uint16_t h, uint8_t s, uint8_t v) {
    h %= 360;
    uint8_t r, g, b;
    if (s == 0) return (rgb_t){v, v, v};

    uint16_t region = h / 60;
    uint16_t rem    = (h % 60) * 255 / 60;
    uint8_t  p      = (uint32_t)v * (255 - s) / 255;
    uint8_t  q      = (uint32_t)v * (255 - (uint32_t)s * rem / 255) / 255;
    uint8_t  t2     = (uint32_t)v * (255 - (uint32_t)s * (255 - rem) / 255) / 255;

    switch (region) {
        case 0:
            r = v;
            g = t2;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t2;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t2;
            g = p;
            b = v;
            break;
        default:
            r = v;
            g = p;
            b = q;
            break;
    }
    return (rgb_t){r, g, b};
}

/**
 * @brief 获取调色板颜色
 */
static inline rgb_t palette_color(uint8_t pid, uint8_t pos) {
    static const rgb_t party[]  = {{255, 0, 50}, {200, 255, 0}, {0, 255, 200}, {50, 0, 255}, {255, 0, 255}};
    static const rgb_t sunset[] = {{120, 0, 40}, {255, 50, 0}, {255, 150, 0}, {100, 0, 100}};
    static const rgb_t lava[]   = {{80, 0, 0}, {255, 0, 0}, {255, 80, 0}, {255, 255, 150}};
    static const rgb_t heat[]   = {{50, 0, 0}, {255, 0, 0}, {255, 150, 0}, {255, 255, 200}};
    static const rgb_t sakura[] = {{255, 150, 200}, {255, 80, 150}, {200, 50, 255}, {150, 200, 255}};
    static const rgb_t ocean[]  = {{0, 30, 80}, {0, 100, 255}, {0, 255, 200}, {100, 200, 255}};
    static const rgb_t aurora[] = {{0, 255, 100}, {0, 150, 255}, {100, 50, 255}, {200, 0, 255}};
    static const rgb_t forest[] = {{0, 40, 0}, {0, 150, 20}, {100, 255, 50}, {200, 255, 100}};
    static const rgb_t cyber[]  = {{0, 255, 255}, {0, 150, 255}, {200, 0, 255}, {255, 0, 150}};
    static const rgb_t mono[]   = {{40, 40, 40}, {255, 255, 255}};

    switch (pid) {
        case PALETTE_RAINBOW:
            return _hsv2rgb((uint16_t)pos * 360 / 255, 255, 255);
        case PALETTE_PARTY:
            return _stops(party, 5, pos);
        case PALETTE_SUNSET:
            return _stops(sunset, 4, pos);
        case PALETTE_LAVA:
            return _stops(lava, 4, pos);
        case PALETTE_HEAT:
            return _stops(heat, 4, pos);
        case PALETTE_SAKURA:
            return _stops(sakura, 4, pos);
        case PALETTE_OCEAN:
            return _stops(ocean, 4, pos);
        case PALETTE_AURORA:
            return _stops(aurora, 4, pos);
        case PALETTE_FOREST:
            return _stops(forest, 4, pos);
        case PALETTE_CYBER:
            return _stops(cyber, 4, pos);
        case PALETTE_MONO:
            return _stops(mono, 2, pos);
        case PALETTE_RANDOM:
            return _hsv2rgb(pos, 255, 255);
        default:
            return _hsv2rgb((uint16_t)pos * 360 / 255, 255, 255);
    }
}

/**
 * @brief 获取调色板颜色（带亮度）
 */
static inline rgb_t palette_color_brightness(uint8_t pid, uint8_t pos, uint8_t brightness) {
    rgb_t c = palette_color(pid, pos);
    return (rgb_t){c.r * brightness / 255, c.g * brightness / 255, c.b * brightness / 255};
}

/**
 * @brief 颜色混合
 */
static inline rgb_t color_blend(rgb_t background, rgb_t foreground, uint8_t alpha) {
    return _lerp_rgb(background, foreground, alpha);
}