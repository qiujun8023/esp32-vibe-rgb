/**
 * @file fx_noise.c
 * @brief 噪声类特效：等离子、极光、火焰等
 */

#include "effects_internal.h"

/**
 * @brief 等离子体效果 (FX_PLASMOID, #21)
 */
void fx_plasmoid(const mic_data_t* d, const settings_t* s) {
    int   w = W, h = H;
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
 * @brief 中间噪声效果 (FX_MIDNOISE, #23)
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
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy <= r * r) {
                    led_set_pixel(x + dx, y + dy, c.r, c.g, c.b);
                }
            }
        }
    }
    s_st.phase += 0.05f;
    s_st.hue_off += 1.0f;
}

/**
 * @brief 噪声计效果 (FX_NOISEMETER, #24)
 */
void fx_noisemeter(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;

    for (int x = 0; x < w; x++) {
        float n     = noise2d(x * 0.2f + s_st.phase, 0);
        int   bar_h = (int)(n * h * d->volume * 2);
        if (bar_h > h) bar_h = h;

        rgb_t c = palette_color(s->palette, x * 255 / w);
        for (int y = 0; y < h; y++) {
            if (y < bar_h) {
                led_set_pixel(x, y, c.r, c.g, c.b);
            } else {
                led_set_pixel(x, y, 0, 0, 0);
            }
        }
    }
    s_st.phase += s->speed / 255.0f;
}

/**
 * @brief 噪声火焰效果 (FX_NOISEFIRE, #20)
 */
void fx_noisefire(const mic_data_t* d, const settings_t* s) {
    int   w = W, h = H;
    float speed = s->speed / 64.0f;
    s_st.phase += speed;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float noise = noise2d(x * 0.5f, y * 0.5f - s_st.phase);
            float mask  = (float)(h - y) / h;
            float v     = noise * mask * (d->volume * 3.0f + 0.5f);

            if (v > 1.0f) v = 1.0f;
            uint8_t bri = (uint8_t)(v * 255);
            rgb_t   c   = palette_color(s->palette, (uint8_t)(v * 64));
            led_set_pixel(x, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
        }
    }
}

/**
 * @brief 极光效果 (FX_AURORA, #22)
 */
void fx_aurora(const mic_data_t* d, const settings_t* s) {
    int   w = W, h = H;
    float speed = s->speed / 255.0f * 0.1f;
    s_st.phase += speed;

    float scale = s->intensity / 128.0f + 0.5f;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float n1 = noise2d(x * 0.2f * scale + s_st.phase, y * 0.1f);
            float n2 = noise2d(y * 0.2f * scale - s_st.phase * 0.5f, x * 0.1f);
            float v  = (n1 + n2) / 2.0f;

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
 * @brief 噪声移动效果 (FX_NOISEMOVE, #25)
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
        led_set_pixel(x + 1, y, c.r, c.g, c.b);
    }
}