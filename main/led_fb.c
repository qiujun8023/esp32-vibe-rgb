// LED 帧缓冲层：像素读写、颜色操作、模糊、淡出
// 无硬件依赖，纯内存操作

#include "led_priv.h"
#include "palettes.h"

#include <string.h>

// ── 模块状态定义 ──────────────────────────────────────────────────────────────
uint8_t s_fb[LED_MAX_COUNT * 3];
uint8_t s_brightness = 160;

// ── 像素写入 ──────────────────────────────────────────────────────────────────
void led_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    int lw = led_width(), lh = led_height();
    if (x < 0 || x >= lw || y < 0 || y >= lh) return;

    int idx = s_lookup[y * lw + x];
    if (idx < 0 || idx >= LED_MAX_COUNT) return;

    s_fb[idx * 3]     = r;
    s_fb[idx * 3 + 1] = g;
    s_fb[idx * 3 + 2] = b;
}

void led_set_pixel_idx(int idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx < 0 || idx >= s_w * s_h || idx >= LED_MAX_COUNT) return;
    s_fb[idx * 3]     = r;
    s_fb[idx * 3 + 1] = g;
    s_fb[idx * 3 + 2] = b;
}

void led_set_pixel_hsv(int x, int y, uint16_t h, uint8_t s, uint8_t v) {
    rgb_t c = _hsv2rgb(h, s, v);
    led_set_pixel(x, y, c.r, c.g, c.b);
}

// ── 像素读取 ──────────────────────────────────────────────────────────────────
void led_get_pixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    int lw = led_width(), lh = led_height();
    if (x < 0 || x >= lw || y < 0 || y >= lh) {
        *r = *g = *b = 0;
        return;
    }
    int idx = s_lookup[y * lw + x];
    if (idx < 0 || idx >= LED_MAX_COUNT) {
        *r = *g = *b = 0;
        return;
    }
    *r = s_fb[idx * 3];
    *g = s_fb[idx * 3 + 1];
    *b = s_fb[idx * 3 + 2];
}

// ── 填充 / 淡出 ───────────────────────────────────────────────────────────────
void led_fill(uint8_t r, uint8_t g, uint8_t b) {
    int count = s_w * s_h;
    for (int i = 0; i < count; i++) led_set_pixel_idx(i, r, g, b);
}

void led_fade_all(uint8_t rate) {
    int count = s_w * s_h;
    for (int i = 0; i < count; i++) {
        uint8_t r = s_fb[i * 3];
        uint8_t g = s_fb[i * 3 + 1];
        uint8_t b = s_fb[i * 3 + 2];
        s_fb[i * 3]     = (r > rate) ? r - rate : 0;
        s_fb[i * 3 + 1] = (g > rate) ? g - rate : 0;
        s_fb[i * 3 + 2] = (b > rate) ? b - rate : 0;
    }
}

// ── 2D 模糊 ───────────────────────────────────────────────────────────────────
// 权重分配：中心 = keep(255-amount)，四邻 = share(amount/4)
// 总权重 = keep + 4*share = (255-amount) + amount = 255，归一化正确
void led_blur2d(uint8_t amount) {
    if (amount == 0) return;

    static uint8_t tmp[LED_MAX_COUNT * 3];
    int            count = s_w * s_h;
    memcpy(tmp, s_fb, count * 3);

    uint16_t keep  = 255 - amount;
    uint16_t share = amount / 4;

    int lw = led_width();
    int lh = led_height();

    for (int y = 0; y < lh; y++) {
        for (int x = 0; x < lw; x++) {
            int idx = s_lookup[y * lw + x];
            if (idx < 0) continue;

            for (int c = 0; c < 3; c++) {
                uint32_t v = (uint32_t)tmp[idx * 3 + c] * keep;

                if (x > 0) {
                    int n = s_lookup[y * lw + (x - 1)];
                    if (n >= 0) v += (uint32_t)tmp[n * 3 + c] * share;
                }
                if (x < lw - 1) {
                    int n = s_lookup[y * lw + (x + 1)];
                    if (n >= 0) v += (uint32_t)tmp[n * 3 + c] * share;
                }
                if (y > 0) {
                    int n = s_lookup[(y - 1) * lw + x];
                    if (n >= 0) v += (uint32_t)tmp[n * 3 + c] * share;
                }
                if (y < lh - 1) {
                    int n = s_lookup[(y + 1) * lw + x];
                    if (n >= 0) v += (uint32_t)tmp[n * 3 + c] * share;
                }

                v >>= 8;
                if (v == 0 && tmp[idx * 3 + c] > 4) v = 1;
                s_fb[idx * 3 + c] = (v > 255) ? 255 : (uint8_t)v;
            }
        }
    }
}

// ── 帧导出 ────────────────────────────────────────────────────────────────────
// 按逻辑行优先导出（y=0 底部，y=lh-1 顶部），供 WebSocket 推流使用
void led_get_fb(uint8_t* buf, int* len) {
    int lw = led_width();
    int lh = led_height();
    for (int y = 0; y < lh; y++) {
        for (int x = 0; x < lw; x++) {
            uint8_t r, g, b;
            led_get_pixel(x, y, &r, &g, &b);
            int base    = (y * lw + x) * 3;
            buf[base]   = r;
            buf[base + 1] = g;
            buf[base + 2] = b;
        }
    }
    *len = lw * lh * 3;
}
