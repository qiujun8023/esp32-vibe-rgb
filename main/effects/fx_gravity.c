/**
 * @file fx_gravity.c
 * @brief 重力类特效：弹跳球、重力计等
 */

#include <esp_random.h>

#include "effects_internal.h"

/**
 * @brief 弹跳球效果 (FX_JUGGLES, #14)
 */
void fx_juggles(const mic_data_t* d, const settings_t* s) {
    fade_out(s->speed / 2 + 100);
    int w = W, h = H;
    int num_balls = s->intensity / 32 + 2;
    if (num_balls > MAX_BALLS) num_balls = MAX_BALLS;

    if (!s_st.balls_init) {
        for (int i = 0; i < MAX_BALLS; i++) {
            s_st.ball_x[i]   = esp_random() % w;
            s_st.ball_y[i]   = esp_random() % h;
            s_st.ball_vx[i]  = (esp_random() % 100 - 50) / 100.0f;
            s_st.ball_vy[i]  = (esp_random() % 100 - 50) / 100.0f;
            s_st.ball_hue[i] = i * 32;
        }
        s_st.balls_init = true;
    }

    float speed_mult = s->speed / 128.0f + 0.1f;
    for (int i = 0; i < num_balls; i++) {
        s_st.ball_x[i] += s_st.ball_vx[i] * speed_mult * (d->volume + 0.5f);
        s_st.ball_y[i] += s_st.ball_vy[i] * speed_mult * (d->volume + 0.5f);

        if (s_st.ball_x[i] < 0) {
            s_st.ball_x[i] = 0;
            s_st.ball_vx[i] *= -1;
        }
        if (s_st.ball_x[i] >= w) {
            s_st.ball_x[i] = w - 1;
            s_st.ball_vx[i] *= -1;
        }
        if (s_st.ball_y[i] < 0) {
            s_st.ball_y[i] = 0;
            s_st.ball_vy[i] *= -1;
        }
        if (s_st.ball_y[i] >= h) {
            s_st.ball_y[i] = h - 1;
            s_st.ball_vy[i] *= -1;
        }

        rgb_t c = palette_color(s->palette, s_st.ball_hue[i] + (uint8_t)s_st.phase);
        led_set_pixel((int)s_st.ball_x[i], (int)s_st.ball_y[i], c.r, c.g, c.b);
        s_st.ball_hue[i]++;
    }
    s_st.phase += 0.5f;
}

/**
 * @brief 重力计效果 (FX_GRAVIMETER, #5)
 */
void fx_gravimeter(const mic_data_t* d, const settings_t* s) {
    int   w = W, h = H;
    float gravity = (10 - s->speed / 32) * 0.05f;

    for (int x = 0; x < w && x < 64; x++) {
        int   band     = x * MIC_BANDS / w;
        float val      = d->bands[band];
        float target_h = val * h;

        if (target_h >= s_st.grav_pos[x]) {
            s_st.grav_pos[x] += (target_h - s_st.grav_pos[x]) * 0.7f;
            s_st.grav_vel[x] = 0;
        } else {
            s_st.grav_vel[x] += gravity;
            s_st.grav_pos[x] -= s_st.grav_vel[x];
            if (s_st.grav_pos[x] < 0) s_st.grav_pos[x] = 0;
        }

        rgb_t c  = palette_color(s->palette, x * 255 / w);
        int   py = (int)s_st.grav_pos[x];
        if (py > h) py = h;

        for (int y = 0; y < h; y++) {
            uint8_t r  = (y < py) ? c.r : 0;
            uint8_t g  = (y < py) ? c.g : 0;
            uint8_t bv = (y < py) ? c.b : 0;
            led_set_pixel(x, y, r, g, bv);
        }
    }
}

/**
 * @brief 重力中心效果 (FX_GRAVCENTER, #6)
 */
void fx_gravcenter(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    int cx = w / 2;

    for (int b = 0; b < MIC_BANDS; b++) {
        float val = d->bands[b];
        int   len = (int)(val * (w / 2) * 1.5f);
        if (len > w / 2) len = w / 2;
        if (len < 1 && val > 0.05f) len = 1;

        rgb_t c = palette_color(s->palette, b * 32);
        int   y = b * h / MIC_BANDS;

        for (int x = 0; x < w; x++) {
            int dist = (x >= cx) ? (x - cx) : (cx - 1 - x);
            if (dist < len) {
                led_set_pixel(x, y, c.r, c.g, c.b);
            } else {
                led_set_pixel(x, y, 0, 0, 0);
            }
        }
    }
}

/**
 * @brief 重力偏心效果 (FX_GRAVCENTRIC, #7)
 */
void fx_gravcentric(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    int cy = h / 2;

    for (int b = 0; b < MIC_BANDS; b++) {
        float val = d->bands[b];
        int   len = (int)(val * (h / 2) * 1.5f);
        if (len > h / 2) len = h / 2;
        if (len < 1 && val > 0.05f) len = 1;

        rgb_t c = palette_color(s->palette, (uint8_t)(s_st.hue_off + b * 20));
        int   x = b * w / MIC_BANDS;

        for (int y = 0; y < h; y++) {
            int dist = (y >= cy) ? (y - cy) : (cy - 1 - y);
            if (dist < len) {
                led_set_pixel(x, y, c.r, c.g, c.b);
            } else {
                led_set_pixel(x, y, 0, 0, 0);
            }
        }
    }
    s_st.hue_off += 0.5f;
}

/**
 * @brief 重力频率效果 (FX_GRAVFREQ, #8)
 */
void fx_gravfreq(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    int cx  = w / 2;
    int bri = (int)(d->volume * 255);

    rgb_t c = palette_color(s->palette, freq_to_color(d->major_peak));

    for (int b = 0; b < MIC_BANDS; b++) {
        int   y   = b * h / MIC_BANDS;
        float val = d->bands[b];
        int   len = (int)(val * (w / 2) * 1.5f);
        if (len > w / 2) len = w / 2;

        for (int x = 0; x < w; x++) {
            int dist = (x >= cx) ? (x - cx) : (cx - 1 - x);
            if (dist < len) {
                led_set_pixel(x, y, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
            } else {
                led_set_pixel(x, y, 0, 0, 0);
            }
        }
    }
}

/**
 * @brief 下落木板效果 (FX_2DFUNKYPLANK, #9)
 */
void fx_2dfunkyplank(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;

    /* 向下滚动 */
    for (int y = 0; y < h - 1; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            led_get_pixel(x, y + 1, &r, &g, &b);
            led_set_pixel(x, y, r, g, b);
        }
    }

    /* 顶部注入新数据 */
    for (int b = 0; b < MIC_BANDS; b++) {
        float val     = d->bands[b];
        int   x_start = b * w / MIC_BANDS;
        int   x_end   = (b + 1) * w / MIC_BANDS;

        if (val > 0.5f) {
            rgb_t c = palette_color(s->palette, b * 32);
            for (int x = x_start; x < x_end; x++) {
                led_set_pixel(x, h - 1, c.r, c.g, c.b);
            }
        } else {
            for (int x = x_start; x < x_end; x++) {
                led_set_pixel(x, h - 1, 0, 0, 0);
            }
        }
    }
}