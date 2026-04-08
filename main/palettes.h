#pragma once
#include <math.h>
#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} rgb_t;

#define PALETTE_COUNT   8
#define PALETTE_RAINBOW 0
#define PALETTE_SUNSET  1
#define PALETTE_OCEAN   2
#define PALETTE_LAVA    3
#define PALETTE_FOREST  4
#define PALETTE_PARTY   5
#define PALETTE_HEAT    6
#define PALETTE_MONO    7

static const char* const PALETTE_NAMES[PALETTE_COUNT] = {"Rainbow", "Sunset", "Ocean", "Lava",
                                                         "Forest",  "Party",  "Heat",  "Mono"};

// 颜色线性插值
static inline rgb_t _lerp_rgb(rgb_t a, rgb_t b, uint8_t t) {
    return (rgb_t){
        .r = (uint8_t)(a.r + (int)(b.r - a.r) * t / 255),
        .g = (uint8_t)(a.g + (int)(b.g - a.g) * t / 255),
        .b = (uint8_t)(a.b + (int)(b.b - a.b) * t / 255),
    };
}

static inline rgb_t _stops(const rgb_t* stops, int n, uint8_t pos) {
    if (n <= 1) return stops[0];
    int seg = (int)pos * (n - 1) / 255;
    if (seg >= n - 1) return stops[n - 1];
    int rem = (int)pos * (n - 1) - seg * 255;
    return _lerp_rgb(stops[seg], stops[seg + 1], (uint8_t)rem);
}

static inline rgb_t _hsv2rgb(uint16_t h, uint8_t s, uint8_t v) {
    h %= 360;
    uint8_t r, g, b;
    if (s == 0) {
        return (rgb_t){v, v, v};
    }
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

// 公共接口
static inline rgb_t palette_color(uint8_t pid, uint8_t pos) {
    static const rgb_t sunset[] = {{255, 0, 80}, {255, 100, 0}, {200, 60, 0}, {100, 0, 80}, {20, 0, 60}};
    static const rgb_t ocean[]  = {{0, 20, 60}, {0, 80, 180}, {0, 200, 255}, {0, 255, 200}, {20, 100, 255}};
    static const rgb_t lava[]   = {{0, 0, 0}, {100, 0, 0}, {255, 0, 0}, {255, 100, 0}, {255, 255, 100}};
    static const rgb_t forest[] = {{0, 20, 0}, {0, 80, 10}, {0, 180, 30}, {0, 255, 100}, {100, 255, 50}};
    static const rgb_t party[]  = {{255, 0, 0},   {255, 128, 0}, {200, 255, 0}, {0, 255, 50},
                                   {0, 200, 255}, {50, 0, 255},  {200, 0, 255}, {255, 0, 128}};
    static const rgb_t heat[]   = {{0, 0, 0}, {100, 0, 0}, {255, 0, 0}, {255, 180, 0}, {255, 255, 200}};
    static const rgb_t mono[]   = {{0, 0, 0}, {255, 255, 255}};

    switch (pid) {
        case PALETTE_RAINBOW:
            return _hsv2rgb((uint16_t)pos * 360 / 255, 255, 255);
        case PALETTE_SUNSET:
            return _stops(sunset, 5, pos);
        case PALETTE_OCEAN:
            return _stops(ocean, 5, pos);
        case PALETTE_LAVA:
            return _stops(lava, 5, pos);
        case PALETTE_FOREST:
            return _stops(forest, 5, pos);
        case PALETTE_PARTY:
            return _stops(party, 8, pos);
        case PALETTE_HEAT:
            return _stops(heat, 5, pos);
        case PALETTE_MONO:
            return _stops(mono, 2, pos);
        default:
            return _hsv2rgb((uint16_t)pos * 360 / 255, 255, 255);
    }
}
