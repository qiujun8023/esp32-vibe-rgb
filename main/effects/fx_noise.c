/**
 * 噪声类效果：PLASMOID, MIDNOISE, NOISEMETER, NOISEFIRE, NOISEMOVE
 */
#include "effects_internal.h"

/**
 * FX_PLASMOID (6) - 等离子体
 */
void fx_plasmoid(const mic_data_t* d, const settings_t* s) {
    int   w     = W, h = H;
    float speed = s->speed / 128.0f;
    s_st.phase += speed;

    float scale = s->intensity / 64.0f + 1.0f;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float v = noise2d(x * scale + s_st.phase, y * scale);
            v += noise2d(y * scale - s_st.phase, x * scale);
            uint8_t color = (uint8_t)(v * 128);
            rgb_t   c     = palette_color(s->palette, color + (uint8_t)s_st.hue_off);
            led_set_pixel(x, y, c.r, c.g, c.b);
        }
    }
    s_st.hue_off += 0.5f;
}

/**
 * FX_MIDNOISE (8) - 中间噪声
 */
void fx_midnoise(const mic_data_t* d, const settings_t* s) {
    fade_out(s->speed / 2 + 100);
    int w = W, h = H;

    for (int i = 0; i < MIC_BANDS; i++) {
        float val = d->bands[i];
        if (val < 0.1f) continue;

        int   x = (int)(noise2d(s_st.phase, i * 0.5f) * w);
        int   y = (int)(noise2d(i * 0.5f, s_st.phase) * h);
        rgb_t c = palette_color(s->palette, i * 32 + (uint8_t)s_st.hue_off);
        
        int r = (int)(val * s->intensity / 64);
        for(int dy=-r; dy<=r; dy++) {
            for(int dx=-r; dx<=r; dx++) {
                if(dx*dx + dy*dy <= r*r) led_set_pixel(x+dx, y+dy, c.r, c.g, c.b);
            }
        }
    }
    s_st.phase += 0.05f;
    s_st.hue_off += 1.0f;
}

/**
 * FX_NOISEMETER (9) - 噪声计
 * 修复：去掉 h-1-y 反转，让柱子从底部向上长
 */
void fx_noisemeter(const mic_data_t* d, const settings_t* s) {
    fade_out(200);
    int w = W, h = H;

    for (int x = 0; x < w; x++) {
        float n = noise2d(x * 0.2f + s_st.phase, 0);
        int   bar_h = (int)(n * h * d->volume * 2);
        if (bar_h > h) bar_h = h;

        rgb_t c = palette_color(s->palette, x * 255 / w);
        for(int y=0; y<bar_h; y++) led_set_pixel(x, y, c.r, c.g, c.b);
    }
    s_st.phase += s->speed / 255.0f;
}

/**
 * FX_NOISEFIRE (16) - 噪声火焰
 * 坐标系：y=0 是底部，y=h-1 是顶部
 * 逻辑：火焰从底部向上燃烧，底部最亮向上渐暗
 */
void fx_noisefire(const mic_data_t* d, const settings_t* s) {
    int   w     = W, h = H;
    float speed = s->speed / 64.0f;
    s_st.phase += speed;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // 火焰噪声逻辑：底部(y=0)最亮，向上渐暗
            // -s_st.phase 让噪声向上移动 = 火焰向上燃烧
            float noise = noise2d(x * 0.5f, y * 0.5f - s_st.phase);
            float mask  = (float)(h - y) / h;  // 底部 mask=1，顶部 mask≈0
            float v     = noise * mask * (d->volume * 3.0f + 0.5f);

            if (v > 1.0f) v = 1.0f;
            uint8_t bri   = (uint8_t)(v * 255);
            // 映射到调色板的前 1/4 (通常是红/橙色)
            rgb_t   c     = palette_color(s->palette, (uint8_t)(v * 64));
            led_set_pixel(x, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
        }
    }
}

/**
 * FX_AURORA (22) - 极光
 */
void fx_aurora(const mic_data_t* d, const settings_t* s) {
    int   w     = W, h = H;
    float speed = s->speed / 255.0f * 0.1f;
    s_st.phase += speed;

    float scale = s->intensity / 128.0f + 0.5f;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // 极光逻辑：多重噪声叠加，产生丝状流动感
            float n1 = noise2d(x * 0.2f * scale + s_st.phase, y * 0.1f);
            float n2 = noise2d(y * 0.2f * scale - s_st.phase * 0.5f, x * 0.1f);
            float v  = (n1 + n2) / 2.0f;

            // 只保留高能量部分，产生丝状感
            v = powf(v, 2.0f) * 2.0f;
            if (v > 1.0f) v = 1.0f;

            uint8_t bri   = (uint8_t)(v * 255 * (d->volume + 0.3f));
            uint8_t color = (uint8_t)(v * 100 + s_st.hue_off);
            rgb_t   c     = palette_color(s->palette, color);
            led_set_pixel(x, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
        }
    }
    s_st.hue_off += 0.2f;
}

/**
 * FX_NOISEMOVE (25) - 噪声移动
 */
void fx_noisemove(const mic_data_t* d, const settings_t* s) {
    fade_out(s->speed / 2 + 128);
    int w = W, h = H;
    s_st.phase += 0.05f;

    for (int i = 0; i < MIC_BANDS; i++) {
        float val = d->bands[i];
        if (val < 0.2f) continue;

        float n = noise2d(s_st.phase, i * 1.0f);
        int   x = (int)(n * w);
        int   y = i * h / MIC_BANDS;
        
        rgb_t c = palette_color(s->palette, i * 32);
        led_set_pixel(x, y, c.r, c.g, c.b);
        led_set_pixel(x+1, y, c.r, c.g, c.b);
    }
}

