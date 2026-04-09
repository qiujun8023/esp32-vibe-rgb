#include "effects_internal.h"
#include <esp_random.h>

/**
 * FX_SPECTRUM (0) - 经典频谱柱
 * 逻辑：y=0 为底, x=0 为左
 */
void fx_spectrum(const mic_data_t* d, const settings_t* s) {
    int  w      = W;
    int  h      = H;
    bool mirror = s->custom3 > 128;
    int  bands  = mirror ? w / 2 : w;

    for (int b = 0; b < bands; b++) {
        int mic_b = b * MIC_BANDS / bands;
        int bar   = (int)(d->bands[mic_b] * h + 0.5f);
        if (bar > h) bar = h;

        for (int y = 0; y < h; y++) {
            rgb_t c = {0, 0, 0};
            if (y < bar) {
                // c1: 0=频带着色, 1=高度着色
                uint8_t pos = (s->custom1 == 0) ? (b * 255 / (bands - 1)) : ((h > 1) ? (uint8_t)(y * 255 / (h - 1)) : 255);
                c = palette_color(s->palette, pos);
            }

            if (s->freq_dir == 0) {
                // 水平排列 (↑) : 频带在 X, 柱子向上长 (Y=0 -> Y=bar)
                led_set_pixel(b, y, c.r, c.g, c.b);
            } else {
                // 垂直排列 (→) : 频带在 Y, 柱子向右长 (X=0 -> X=bar)
                led_set_pixel(y, b, c.r, c.g, c.b);
            }
        }

        // 峰值点绘制
        if (s->custom2 > 64) {
            if (bar > s_st.peak_hold[b]) s_st.peak_hold[b] = (float)bar;
            s_st.peak_hold[b] -= 0.05f + (s->speed / 255.0f * 0.2f);
            if (s_st.peak_hold[b] < 0) s_st.peak_hold[b] = 0;

            int py = (int)s_st.peak_hold[b];
            if (py > 0 && py < h) {
                if (s->freq_dir == 0) led_set_pixel(b, py, 255, 255, 255);
                else led_set_pixel(py, b, 255, 255, 255);
            }
        }
    }

    if (mirror && s->freq_dir == 0) {
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
 * FX_2DGEQ (1) - 频谱均衡器
 */
void fx_2dgeq(const mic_data_t* d, const settings_t* s) {
    led_clear();
    int w = W, h = H;
    for (int x = 0; x < w; x++) {
        int   band = x * MIC_BANDS / w;
        float val  = d->bands[band];
        int   bar_h = (int)(val * h);
        if (bar_h < 1 && val > 0.05f) bar_h = 1;
        rgb_t c = palette_color(s->palette, x * 255 / w);
        
        if (s->freq_dir == 0) {
            // 水平排列: 向上长
            for (int y = 0; y < bar_h; y++) led_set_pixel(x, y, c.r, c.g, c.b);
        } else {
            // 垂直排列: 向右长
            for (int y = 0; y < bar_h; y++) led_set_pixel(y, x, c.r, c.g, c.b);
        }

        // 峰值点
        if (bar_h > (int)s_st.geq_peak[x]) s_st.geq_peak[x] = (float)bar_h;
        else if (s_st.frame % 4 == 0 && s_st.geq_peak[x] > 0) s_st.geq_peak[x]--;
        
        int py = (int)s_st.geq_peak[x];
        if (py >= 0 && py < h) {
            if (s->freq_dir == 0) led_set_pixel(x, py, 255, 255, 255);
            else led_set_pixel(py, x, 255, 255, 255);
        }
    }
    s_st.frame++;
}

/**
 * FX_WATERFALL (4) - 瀑布流
 * 逻辑：向下流动 (Y 坐标减小)
 */
void fx_waterfall(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    // 现有像素向下滚动：(x, y) 搬运到 (x, y-1)
    for (int y = 0; y < h - 1; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            led_get_pixel(x, y + 1, &r, &g, &b);
            led_set_pixel(x, y, r, g, b);
        }
    }
    // 顶部注入新数据 (y = h-1)
    rgb_t c   = palette_color(s->palette, freq_to_color(d->major_peak) + (uint8_t)s_st.hue_off);
    int   bri = (int)(d->volume * 255);
    for (int x = 0; x < w; x++) {
        float band_v = d->bands[x * MIC_BANDS / w];
        int p_bri = (int)(bri * band_v);
        led_set_pixel(x, h - 1, c.r * p_bri / 255, c.g * p_bri / 255, c.b * p_bri / 255);
    }
    s_st.hue_off += 0.5f;
}

/**
 * FX_BINMAP (3) - 频谱映射
 * 逻辑：底部向上辐射强光
 */
void fx_binmap(const mic_data_t* d, const settings_t* s) {
    fade_out(64);
    int w = W, h = H;
    for (int x = 0; x < w; x++) {
        float val = d->bands[x * MIC_BANDS / w];
        int bri = (int)(val * 255);
        rgb_t c = palette_color(s->palette, x * 255 / w + (uint8_t)s_st.phase);
        for (int y = 0; y < h; y++) {
            uint8_t y_bri = bri * (h - y) / h;
            led_set_pixel(x, y, c.r * y_bri / 255, c.g * y_bri / 255, c.b * y_bri / 255);
        }
    }
    s_st.phase += 0.5f;
}

/**
 * FX_FREQWAVE (11) - 频率波 (2D 中心扩散版)
 */
void fx_freqwave(const mic_data_t* d, const settings_t* s) {
    fade_out(30);
    float freq  = d->major_peak;
    rgb_t c     = palette_color(s->palette, freq_to_color(freq));
    int   bri   = (int)(d->volume * 255);
    int   w = W, h = H, cx = w / 2, cy = h / 2;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            if (x < cx) { led_get_pixel(x+1, y, &r,&g,&b); led_set_pixel(x,y,r,g,b); }
            else if (x > cx) { led_get_pixel(x-1, y, &r,&g,&b); led_set_pixel(x,y,r,g,b); }
            if (y < cy) { led_get_pixel(x, y+1, &r,&g,&b); led_set_pixel(x,y,r,g,b); }
            else if (y > cy) { led_get_pixel(x, y-1, &r,&g,&b); led_set_pixel(x,y,r,g,b); }
        }
    }
    led_set_pixel(cx, cy, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(cx-1, cy, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(cx, cy-1, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(cx-1, cy-1, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
}

/**
 * FX_FREQPIXELS (17) - 频率像素
 */
void fx_freqpixels(const mic_data_t* d, const settings_t* s) {
    fade_out(255 - s->speed / 2);
    int count = s->intensity / 16 + 1;
    for (int i = 0; i < count; i++) {
        int x = esp_random() % W, y = esp_random() % H;
        rgb_t c = palette_color(s->palette, freq_to_color(d->major_peak) + (uint8_t)esp_random() % 32);
        int bri = (int)(d->volume * 255);
        led_set_pixel(x, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    }
}

/**
 * FX_FREQMAP (18) - 频率映射
 */
void fx_freqmap(const mic_data_t* d, const settings_t* s) {
    fade_out(200);
    rgb_t c = palette_color(s->palette, freq_to_color(d->major_peak));
    int bri = (int)(d->volume * 255);
    int pos = freq_to_pos(d->major_peak, W * H);
    int x = pos % W, y = pos / W;
    if (y >= H) y = H - 1;
    led_set_pixel(x, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(x + 1, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
    led_set_pixel(x, y + 1, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
}
