/**
 * @file fx_freq.c
 * @brief 频谱类特效：频谱柱、均衡器、瀑布流等
 */

#include <esp_random.h>

#include "effects_internal.h"

/**
 * @brief 频谱柱效果 (FX_SPECTRUM, #0)
 */
void fx_spectrum(const mic_data_t* d, const settings_t* s) {
    int  w      = W;
    int  h      = H;
    bool mirror = s->custom3 > 128;
    int  bands  = mirror ? w / 2 : w;

    if (bands <= 0) return;

    for (int b = 0; b < bands; b++) {
        int mic_b = b * MIC_BANDS / bands;
        if (mic_b >= MIC_BANDS) mic_b = MIC_BANDS - 1;
        int bar = (int)(d->bands[mic_b] * h + 0.5f);
        if (bar > h) bar = h;

        /* 峰值计算 */
        int py = -1;
        if (s->custom2 > 64) {
            if (bar > s_st.peak_hold[b]) s_st.peak_hold[b] = (float)bar;
            s_st.peak_hold[b] -= 0.05f + (s->speed / 255.0f * 0.2f);
            if (s_st.peak_hold[b] < 0) s_st.peak_hold[b] = 0;
            py = (int)s_st.peak_hold[b];
        }

        /* 逐像素绘制 */
        for (int y = 0; y < h; y++) {
            uint8_t r, g, bv;
            if (y < bar) {
                uint8_t pos = (s->custom1 == 0) ? ((bands > 1) ? (uint8_t)(b * 255 / (bands - 1)) : 0)
                                                : ((h > 1) ? (uint8_t)(y * 255 / (h - 1)) : 255);
                rgb_t   c   = palette_color(s->palette, pos);
                r           = c.r;
                g           = c.g;
                bv          = c.b;
            } else if (y == py && py > 0 && py < h) {
                r = g = bv = 255;
            } else {
                r = g = bv = 0;
            }
            led_set_pixel(b, y, r, g, bv);
        }
    }

    /* 镜像显示 */
    if (mirror) {
        for (int b = 0; b < w / 2; b++) {
            for (int y = 0; y < h; y++) {
                uint8_t r, g, bl;
                led_get_pixel(b, y, &r, &g, &bl);
                led_set_pixel(w - 1 - b, y, r, g, bl);
            }
        }
    }
}

/**
 * @brief 频谱均衡器效果 (FX_2DGEQ, #1)
 */
void fx_2dgeq(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    for (int x = 0; x < w; x++) {
        int   band  = x * MIC_BANDS / w;
        float val   = d->bands[band];
        int   bar_h = (int)(val * h);
        if (bar_h < 1 && val > 0.05f) bar_h = 1;
        rgb_t c = palette_color(s->palette, x * 255 / w);

        /* 峰值追踪 */
        if (bar_h > (int)s_st.geq_peak[x]) {
            s_st.geq_peak[x] = (float)bar_h;
        } else if (s_st.frame % 4 == 0 && s_st.geq_peak[x] > 0) {
            s_st.geq_peak[x]--;
        }
        int py = (int)s_st.geq_peak[x];

        for (int y = 0; y < h; y++) {
            uint8_t r, g, bv;
            if (y < bar_h) {
                r  = c.r;
                g  = c.g;
                bv = c.b;
            } else if (y == py && py >= 0 && py < h) {
                r = g = bv = 255;
            } else {
                r = g = bv = 0;
            }
            led_set_pixel(x, y, r, g, bv);
        }
    }
    s_st.frame++;
}

/**
 * @brief 瀑布流效果 (FX_WATERFALL, #4)
 */
void fx_waterfall(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;

    /* 向下滚动 */
    for (int y = 0; y < h - 1; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            led_get_pixel(x, y + 1, &r, &g, &b);
            led_set_pixel(x, y, r, g, b);
        }
    }

    /* 顶部新数据 */
    float freq_color = (d->major_peak > 0) ? freq_to_color(d->major_peak) : 0;
    rgb_t c          = palette_color(s->palette, (uint8_t)freq_color + (uint8_t)s_st.hue_off);
    int   bri        = (int)(d->volume * 255);

    for (int x = 0; x < w; x++) {
        float band_v = d->bands[x * MIC_BANDS / w];
        int   p_bri  = (int)(bri * band_v);
        led_set_pixel(x, h - 1, c.r * p_bri / 255, c.g * p_bri / 255, c.b * p_bri / 255);
    }
    s_st.hue_off += 0.5f;
}

/**
 * @brief 频谱映射效果 (FX_BINMAP, #3)
 */
void fx_binmap(const mic_data_t* d, const settings_t* s) {
    fade_out(64);
    int w = W, h = H;

    for (int x = 0; x < w; x++) {
        float val = d->bands[x * MIC_BANDS / w];
        int   bri = (int)(val * 255);
        rgb_t c   = palette_color(s->palette, x * 255 / w + (uint8_t)s_st.phase);

        for (int y = 0; y < h; y++) {
            uint8_t y_bri = bri * (h - y) / h;
            led_set_pixel(x, y, c.r * y_bri / 255, c.g * y_bri / 255, c.b * y_bri / 255);
        }
    }
    s_st.phase += 0.5f;
}

/**
 * @brief 频率波效果 (FX_FREQWAVE, #11)
 */
void fx_freqwave(const mic_data_t* d, const settings_t* s) {
    fade_out(30);
    float freq = d->major_peak;
    rgb_t c    = palette_color(s->palette, freq_to_color(freq));
    int   bri  = (int)(d->volume * 255);
    int   w = W, h = H, cx = w / 2, cy = h / 2;

    /* 波纹扩散 */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            if (x < cx) {
                led_get_pixel(x + 1, y, &r, &g, &b);
                led_set_pixel(x, y, r, g, b);
            } else if (x > cx) {
                led_get_pixel(x - 1, y, &r, &g, &b);
                led_set_pixel(x, y, r, g, b);
            }
            if (y < cy) {
                led_get_pixel(x, y + 1, &r, &g, &b);
                led_set_pixel(x, y, r, g, b);
            } else if (y > cy) {
                led_get_pixel(x, y - 1, &r, &g, &b);
                led_set_pixel(x, y, r, g, b);
            }
        }
    }

    /* 中心亮点 */
    led_set_pixel(cx, cy, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(cx - 1, cy, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(cx, cy - 1, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(cx - 1, cy - 1, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
}

/**
 * @brief 频率像素效果 (FX_FREQPIXELS, #17)
 */
void fx_freqpixels(const mic_data_t* d, const settings_t* s) {
    fade_out(255 - s->speed / 2);
    int count = s->intensity / 16 + 1;

    for (int i = 0; i < count; i++) {
        int   x          = esp_random() % W;
        int   y          = esp_random() % H;
        float freq_color = (d->major_peak > 0) ? freq_to_color(d->major_peak) : 0;
        rgb_t c          = palette_color(s->palette, (uint8_t)freq_color + (uint8_t)(esp_random() % 32));
        int   bri        = (int)(d->volume * 255);
        led_set_pixel(x, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    }
}

/**
 * @brief 频率映射效果 (FX_FREQMAP, #18)
 */
void fx_freqmap(const mic_data_t* d, const settings_t* s) {
    fade_out(200);
    int w = W, h = H;

    for (int b = 0; b < MIC_BANDS; b++) {
        float val = d->bands[b];
        if (val < 0.05f) continue;

        int     bri       = (int)(val * 255);
        uint8_t color_pos = b * 255 / MIC_BANDS + (uint8_t)s_st.phase;
        rgb_t   c         = palette_color(s->palette, color_pos);

        int area_w   = w / MIC_BANDS;
        int center_x = b * area_w + area_w / 2;

        int lit_h = (int)(val * h);
        if (lit_h < 1) lit_h = 1;

        for (int y = 0; y < lit_h; y++) {
            for (int dx = -1; dx <= 1; dx++) {
                int px = center_x + dx;
                if (px >= 0 && px < w) {
                    uint8_t y_bri = bri * (h - y) / h;
                    led_set_pixel(px, y, c.r * y_bri / 255, c.g * y_bri / 255, c.b * y_bri / 255);
                }
            }
        }
    }
    s_st.phase += 0.5f;
}