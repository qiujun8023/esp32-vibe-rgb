#include "effects_internal.h"
#include <esp_random.h>

/**
 * FX_SPECTRUM (0) - 经典频谱柱
 * 优化：改用 fade_out 让柱子变化更平滑
 * 逻辑：y=0 为底, x=0 为左
 */
void fx_spectrum(const mic_data_t* d, const settings_t* s) {
    fade_out(128);  // 添加残影，让柱子变化更平滑
    int  w      = W;
    int  h      = H;
    bool mirror = s->custom3 > 128;
    int  bands  = mirror ? w / 2 : w;

    for (int b = 0; b < bands; b++) {
        int mic_b = b * MIC_BANDS / bands;
        int bar   = (int)(d->bands[mic_b] * h + 0.5f);
        if (bar > h) bar = h;

        // 只绘制柱子部分（不绘制黑色区域，让 fade_out 处理）
        for (int y = 0; y < bar; y++) {
            // c1: 0=频带着色, 1=高度着色
            uint8_t pos = (s->custom1 == 0) ? (b * 255 / (bands - 1)) : ((h > 1) ? (uint8_t)(y * 255 / (h - 1)) : 255);
            rgb_t c = palette_color(s->palette, pos);

            if (s->freq_dir == 0) {
                led_set_pixel(b, y, c.r, c.g, c.b);
            } else {
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
 * 修复：改用 fade_out 替代 led_clear，消除频闪
 */
void fx_2dgeq(const mic_data_t* d, const settings_t* s) {
    fade_out(64);  // 改用 fade_out 替代 led_clear，消除频闪
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
 * 坐标系：y=0 是底部，y=h-1 是顶部
 * 逻辑：从顶部向下流动，新数据从顶部注入
 */
void fx_waterfall(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    // 从高处(y+1)向低处(y)滚动 = 从上往下流
    for (int y = 0; y < h - 1; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            led_get_pixel(x, y + 1, &r, &g, &b);  // 从高处获取
            led_set_pixel(x, y, r, g, b);          // 向低处移动
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
 * 修复：使用多频带映射，让整个矩阵都有灯光分布
 */
void fx_freqmap(const mic_data_t* d, const settings_t* s) {
    fade_out(200);
    int w = W, h = H;
    
    // 遍历所有频带，映射到矩阵不同区域
    for (int b = 0; b < MIC_BANDS; b++) {
        float val = d->bands[b];
        if (val < 0.05f) continue;  // 跳过太弱的频带
        
        int bri = (int)(val * 255);
        uint8_t color_pos = b * 255 / MIC_BANDS + (uint8_t)s_st.phase;
        rgb_t c = palette_color(s->palette, color_pos);
        
        // 将频带映射到矩阵区域 (对数分布)
        int area_w = w / MIC_BANDS;
        int base_x = b * area_w;
        int center_x = base_x + area_w / 2;
        
        // 根据强度决定亮灯高度
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
