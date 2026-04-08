#include "led.h"

#include <esp_log.h>
#include <led_strip.h>
#include <string.h>

static const char* TAG = "led";

static led_strip_handle_t s_strip;
static uint8_t            s_brightness = 160;
static int                s_w = 8, s_h = 8;
static uint8_t            s_serpentine = 1;
static uint8_t            s_start      = 0;  // 0=左下 1=右下 2=左上 3=右上

// 帧缓冲（原始亮度，不含全局 brightness 缩放）
static uint8_t s_fb[LED_MAX_COUNT * 3];

// ── 矩阵坐标 → LED 编号 ────────────────────────────
// 坐标系：x∈[0,W-1]  y∈[0,H-1]，左下=(0,0)
// 物理 LED 0 在 led_start 角
static int matrix_idx(int x, int y) {
    // 翻转坐标以适配起始角
    int px = (s_start & 1) ? (s_w - 1 - x) : x;  // bit0: 左/右
    int py = (s_start & 2) ? (s_h - 1 - y) : y;  // bit1: 下/上
    if (s_serpentine && (py & 1)) px = (s_w - 1) - px;
    return py * s_w + px;
}

void led_init(const settings_t* st) {
    s_w          = st->led_w ? st->led_w : 8;
    s_h          = st->led_h ? st->led_h : 8;
    s_serpentine = st->led_serpentine;
    s_start      = st->led_start;
    s_brightness = st->brightness;

    int count = s_w * s_h;
    memset(s_fb, 0, count * 3);

    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = st->led_gpio,
        .max_leds         = count,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma    = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "init gpio=%d w=%d h=%d serpentine=%d start=%d", st->led_gpio, s_w, s_h, s_serpentine, s_start);
}

void led_apply_settings(const settings_t* st) {
    s_brightness = st->brightness;
    s_serpentine = st->led_serpentine;
    s_start      = st->led_start;
}

static uint8_t sc(uint8_t v) {
    return (uint8_t)((uint32_t)v * s_brightness / 255);
}

void led_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= s_w || y < 0 || y >= s_h) return;
    int idx           = matrix_idx(x, y);
    s_fb[idx * 3]     = r;
    s_fb[idx * 3 + 1] = g;
    s_fb[idx * 3 + 2] = b;
    led_strip_set_pixel(s_strip, idx, sc(r), sc(g), sc(b));
}

void led_set_pixel_idx(int idx, uint8_t r, uint8_t g, uint8_t b) {
    int count = s_w * s_h;
    if (idx < 0 || idx >= count) return;
    s_fb[idx * 3]     = r;
    s_fb[idx * 3 + 1] = g;
    s_fb[idx * 3 + 2] = b;
    led_strip_set_pixel(s_strip, idx, sc(r), sc(g), sc(b));
}

void led_get_pixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (x < 0 || x >= s_w || y < 0 || y >= s_h) {
        *r = *g = *b = 0;
        return;
    }
    int idx = matrix_idx(x, y);
    *r      = s_fb[idx * 3];
    *g      = s_fb[idx * 3 + 1];
    *b      = s_fb[idx * 3 + 2];
}

void led_set_pixel_hsv(int x, int y, uint16_t h, uint8_t s, uint8_t v) {
    h %= 360;
    uint8_t r2, g2, b2;
    if (s == 0) {
        r2 = g2 = b2 = v;
    } else {
        uint16_t reg = h / 60, rem = (h % 60) * 255 / 60;
        uint8_t  p  = (uint32_t)v * (255 - s) / 255;
        uint8_t  q  = (uint32_t)v * (255 - (uint32_t)s * rem / 255) / 255;
        uint8_t  t2 = (uint32_t)v * (255 - (uint32_t)s * (255 - rem) / 255) / 255;
        switch (reg) {
            case 0:
                r2 = v;
                g2 = t2;
                b2 = p;
                break;
            case 1:
                r2 = q;
                g2 = v;
                b2 = p;
                break;
            case 2:
                r2 = p;
                g2 = v;
                b2 = t2;
                break;
            case 3:
                r2 = p;
                g2 = q;
                b2 = v;
                break;
            case 4:
                r2 = t2;
                g2 = p;
                b2 = v;
                break;
            default:
                r2 = v;
                g2 = p;
                b2 = q;
                break;
        }
    }
    led_set_pixel(x, y, r2, g2, b2);
}

void led_fill(uint8_t r, uint8_t g, uint8_t b) {
    for (int y = 0; y < s_h; y++)
        for (int x = 0; x < s_w; x++) led_set_pixel(x, y, r, g, b);
}

void led_clear(void) {
    memset(s_fb, 0, s_w * s_h * 3);
    led_strip_clear(s_strip);
}

void led_flush(void) {
    led_strip_refresh(s_strip);
}

void led_fade_all(uint8_t rate) {
    for (int y = 0; y < s_h; y++)
        for (int x = 0; x < s_w; x++) {
            uint8_t r, g, b;
            led_get_pixel(x, y, &r, &g, &b);
            r = r > rate ? r - rate : 0;
            g = g > rate ? g - rate : 0;
            b = b > rate ? b - rate : 0;
            led_set_pixel(x, y, r, g, b);
        }
}

void led_blur2d(uint8_t amount) {
    if (amount == 0) return;
    static uint8_t tmp[LED_MAX_COUNT * 3];
    int            count = s_w * s_h;
    memcpy(tmp, s_fb, count * 3);

    uint8_t keep  = 255 - amount;
    uint8_t share = amount / 4;

    for (int y = 0; y < s_h; y++) {
        for (int x = 0; x < s_w; x++) {
            for (int c = 0; c < 3; c++) {
                uint16_t v = (uint16_t)tmp[(matrix_idx(x, y)) * 3 + c] * keep / 255;
                if (x > 0) v += (uint16_t)tmp[(matrix_idx(x - 1, y)) * 3 + c] * share / 255;
                if (x < s_w - 1) v += (uint16_t)tmp[(matrix_idx(x + 1, y)) * 3 + c] * share / 255;
                if (y > 0) v += (uint16_t)tmp[(matrix_idx(x, y - 1)) * 3 + c] * share / 255;
                if (y < s_h - 1) v += (uint16_t)tmp[(matrix_idx(x, y + 1)) * 3 + c] * share / 255;
                uint8_t* fb = s_fb + matrix_idx(x, y) * 3 + c;
                *fb         = v > 255 ? 255 : (uint8_t)v;
                led_strip_set_pixel(s_strip, matrix_idx(x, y), sc(s_fb[matrix_idx(x, y) * 3]),
                                    sc(s_fb[matrix_idx(x, y) * 3 + 1]), sc(s_fb[matrix_idx(x, y) * 3 + 2]));
            }
        }
    }
}

void led_get_fb(uint8_t* buf, int* len) {
    int n = s_w * s_h;
    // 按逻辑坐标顺序输出（左下→右→上）
    for (int y = 0; y < s_h; y++) {
        for (int x = 0; x < s_w; x++) {
            int idx                    = matrix_idx(x, y);
            buf[(y * s_w + x) * 3]     = s_fb[idx * 3];
            buf[(y * s_w + x) * 3 + 1] = s_fb[idx * 3 + 1];
            buf[(y * s_w + x) * 3 + 2] = s_fb[idx * 3 + 2];
        }
    }
    *len = n * 3;
}

int led_width(void) {
    return s_w;
}
int led_height(void) {
    return s_h;
}
int led_count(void) {
    return s_w * s_h;
}
