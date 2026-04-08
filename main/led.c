#include "led.h"

#include <esp_log.h>
#include <led_strip.h>
#include <string.h>

#include "palettes.h"

static const char* TAG = "led";

static led_strip_handle_t s_strip;
static uint8_t            s_brightness = 160;
static int                s_w = 8, s_h = 8;
static uint8_t            s_serpentine = 1;
static uint8_t            s_start      = 0;
static uint8_t            s_rotation   = 0;

static uint8_t s_fb[LED_MAX_COUNT * 3];
static int16_t s_lookup[LED_MAX_COUNT];

/**
 * 计算物理 LED 索引
 */
static int get_physical_idx(int px, int py) {
    if (px < 0 || px >= s_w || py < 0 || py >= s_h) return -1;

    int x = (s_start & 1) ? (s_w - 1 - px) : px;
    int y = (s_start & 2) ? (s_h - 1 - py) : py;

    if (s_serpentine && (y & 1)) {
        x = (s_w - 1) - x;
    }

    return y * s_w + x;
}

/**
 * 更新坐标映射查找表
 */
static void update_lookup(void) {
    int lw = led_width();
    int lh = led_height();

    for (int ly = 0; ly < lh; ly++) {
        for (int lx = 0; lx < lw; lx++) {
            int px, py;
            switch (s_rotation) {
                case 1:
                    px = ly;
                    py = s_h - 1 - lx;
                    break;
                case 2:
                    px = s_w - 1 - lx;
                    py = s_h - 1 - ly;
                    break;
                case 3:
                    px = s_w - 1 - ly;
                    py = lx;
                    break;
                default:
                    px = lx;
                    py = ly;
                    break;
            }
            s_lookup[ly * lw + lx] = get_physical_idx(px, py);
        }
    }
}

static inline uint8_t scale_brightness(uint8_t v) {
    return (uint8_t)((uint32_t)v * s_brightness / 255);
}

void led_init(const settings_t* st) {
    s_w          = st->led_w ? st->led_w : 8;
    s_h          = st->led_h ? st->led_h : 8;
    s_serpentine = st->led_serpentine;
    s_start      = st->led_start;
    s_rotation   = st->led_rotation;
    s_brightness = st->brightness;

    int count = s_w * s_h;
    if (count > LED_MAX_COUNT) {
        ESP_LOGE(TAG, "led count %d exceeds max %d", count, LED_MAX_COUNT);
        count = LED_MAX_COUNT;
    }

    memset(s_fb, 0, sizeof(s_fb));
    update_lookup();

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

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create led strip: %s", esp_err_to_name(err));
        return;
    }
    led_strip_clear(s_strip);

    ESP_LOGI(TAG, "led init ok, gpio: %d, size: %dx%d", st->led_gpio, s_w, s_h);
}

void led_apply_settings(const settings_t* st) {
    s_brightness = st->brightness;
    s_serpentine = st->led_serpentine;
    s_start      = st->led_start;
    s_rotation   = st->led_rotation;
    s_w          = st->led_w ? st->led_w : 8;
    s_h          = st->led_h ? st->led_h : 8;
    update_lookup();
}

void led_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    int lw = led_width();
    int lh = led_height();
    if (x < 0 || x >= lw || y < 0 || y >= lh) return;

    int idx = s_lookup[y * lw + x];
    if (idx < 0 || idx >= LED_MAX_COUNT) return;

    s_fb[idx * 3]     = r;
    s_fb[idx * 3 + 1] = g;
    s_fb[idx * 3 + 2] = b;

    led_strip_set_pixel(s_strip, idx, scale_brightness(r), scale_brightness(g), scale_brightness(b));
}

void led_set_pixel_idx(int idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx < 0 || idx >= s_w * s_h || idx >= LED_MAX_COUNT) return;
    s_fb[idx * 3]     = r;
    s_fb[idx * 3 + 1] = g;
    s_fb[idx * 3 + 2] = b;
    led_strip_set_pixel(s_strip, idx, scale_brightness(r), scale_brightness(g), scale_brightness(b));
}

void led_get_pixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    int lw = led_width();
    int lh = led_height();
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

void led_set_pixel_hsv(int x, int y, uint16_t h, uint8_t s, uint8_t v) {
    rgb_t c = _hsv2rgb(h, s, v);
    led_set_pixel(x, y, c.r, c.g, c.b);
}

void led_fill(uint8_t r, uint8_t g, uint8_t b) {
    int count = s_w * s_h;
    for (int i = 0; i < count; i++) {
        led_set_pixel_idx(i, r, g, b);
    }
}

void led_clear(void) {
    memset(s_fb, 0, sizeof(s_fb));
    if (s_strip) {
        led_strip_clear(s_strip);
    }
}

void led_flush(void) {
    if (s_strip) {
        led_strip_refresh(s_strip);
    }
}

void led_fade_all(uint8_t rate) {
    int count = s_w * s_h;
    for (int i = 0; i < count; i++) {
        uint8_t r = s_fb[i * 3];
        uint8_t g = s_fb[i * 3 + 1];
        uint8_t b = s_fb[i * 3 + 2];

        r = (r > rate) ? r - rate : 0;
        g = (g > rate) ? g - rate : 0;
        b = (b > rate) ? b - rate : 0;

        led_set_pixel_idx(i, r, g, b);
    }
}

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
                    int n_idx = s_lookup[y * lw + (x - 1)];
                    if (n_idx >= 0) v += (uint32_t)tmp[n_idx * 3 + c] * share;
                }
                if (x < lw - 1) {
                    int n_idx = s_lookup[y * lw + (x + 1)];
                    if (n_idx >= 0) v += (uint32_t)tmp[n_idx * 3 + c] * share;
                }
                if (y > 0) {
                    int n_idx = s_lookup[(y - 1) * lw + x];
                    if (n_idx >= 0) v += (uint32_t)tmp[n_idx * 3 + c] * share;
                }
                if (y < lh - 1) {
                    int n_idx = s_lookup[(y + 1) * lw + x];
                    if (n_idx >= 0) v += (uint32_t)tmp[n_idx * 3 + c] * share;
                }

                v /= 255;
                s_fb[idx * 3 + c] = (v > 255) ? 255 : (uint8_t)v;
            }
            led_strip_set_pixel(s_strip, idx, scale_brightness(s_fb[idx * 3]), scale_brightness(s_fb[idx * 3 + 1]),
                                scale_brightness(s_fb[idx * 3 + 2]));
        }
    }
}

void led_get_fb(uint8_t* buf, int* len) {
    int lw = led_width();
    int lh = led_height();
    for (int y = 0; y < lh; y++) {
        for (int x = 0; x < lw; x++) {
            int idx = s_lookup[y * lw + x];
            if (idx >= 0) {
                buf[(y * lw + x) * 3]     = s_fb[idx * 3];
                buf[(y * lw + x) * 3 + 1] = s_fb[idx * 3 + 1];
                buf[(y * lw + x) * 3 + 2] = s_fb[idx * 3 + 2];
            } else {
                memset(&buf[(y * lw + x) * 3], 0, 3);
            }
        }
    }
    *len = lw * lh * 3;
}

int led_width(void) {
    return (s_rotation & 1) ? s_h : s_w;
}
int led_height(void) {
    return (s_rotation & 1) ? s_w : s_h;
}
int led_count(void) {
    return s_w * s_h;
}