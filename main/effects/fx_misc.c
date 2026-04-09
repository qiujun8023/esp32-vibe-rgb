/**
 * 其他效果：PIXELS, PIXELWAVE, MATRIPIX, PUDDLES, PUDDLEPEAK,
 *           RIPPLEPEAK, DJLIGHT, 2DCENTERBARS, BLURZ
 */
#include "effects_internal.h"

#include <esp_random.h>

/**
 * FX_PIXELS (1) - 随机像素
 */
void fx_pixels(const mic_data_t* d, const settings_t* s) {
    fade_out(s->speed);
    int count = s->intensity / 16 + 1;
    int bri   = (int)(d->volume * 255);
    if (bri > 255) bri = 255;

    int w = led_width(), h = led_height();
    for (int i = 0; i < count; i++) {
        int     x       = esp_random() % w;
        int     y       = esp_random() % h;
        uint8_t color   = (uint8_t)(s_st.hue_off + i * 8);
        rgb_t   c       = palette_color(s->palette, color);
        rgb_t   bg      = {0, 0, 0};
        rgb_t   blended = color_blend(bg, c, bri);
        led_set_pixel(x, y, blended.r, blended.g, blended.b);
    }
    s_st.hue_off = fmodf(s_st.hue_off + 0.5f, 255);
}

/**
 * FX_PIXELWAVE (2) - 像素波 (2D 圆形扩散)
 */
void fx_pixelwave(const mic_data_t* d, const settings_t* s) {
    fade_out(240);
    int bri = (int)(d->volume * s->intensity);
    if (bri > 255) bri = 255;

    rgb_t c  = palette_color(s->palette, (uint8_t)s_st.hue_off);
    int   w  = W, h = H;
    int   cx = w / 2, cy = h / 2;

    float speed = s->speed / 128.0f + 0.1f;
    s_st.phase += speed;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dist = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
            float wave = sinf(dist - s_st.phase) * 0.5f + 0.5f;
            uint8_t wb = (uint8_t)(bri * wave);
            if (wb > 10) {
                led_set_pixel(x, y, c.r * wb / 255, c.g * wb / 255, c.b * wb / 255);
            }
        }
    }

    s_st.hue_off = fmodf(s_st.hue_off + 0.5f, 255);
}

/**
 * FX_MATRIPIX (4) - 矩阵像素
 */
void fx_matripix(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    int bri = (int)(d->volume * s->intensity);
    if (bri > 255) bri = 255;

    // 向左滚动
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w - 1; x++) {
            uint8_t r, g, b;
            led_get_pixel(x + 1, y, &r, &g, &b);
            led_set_pixel(x, y, r, g, b);
        }
    }

    // 在右侧注入
    int rx = w - 1;
    for (int b = 0; b < h; b++) {
        int   band  = b * MIC_BANDS / h;
        rgb_t c     = palette_color(s->palette, (uint8_t)(band * 255 / MIC_BANDS));
        float val   = d->bands[band];
        int   b_bri = (int)(val * bri);
        led_set_pixel(rx, b, c.r * b_bri / 255, c.g * b_bri / 255, c.b * b_bri / 255);
    }
}

/**
 * FX_PUDDLES (7) - 水塘
 */
void fx_puddles(const mic_data_t* d, const settings_t* s) {
    fade_out(255 - s->speed / 2);

    int bri = (int)(d->volume * 255);
    if (bri > 30) {
        int   size = (int)(d->volume * s->intensity / 100) + 1;
        int   x    = esp_random() % W;
        int   y    = esp_random() % H;
        rgb_t c    = palette_color(s->palette, (uint8_t)s_st.hue_off);

        for (int dy = -size; dy <= size; dy++) {
            for (int dx = -size; dx <= size; dx++) {
                if (dx * dx + dy * dy <= size * size) {
                    led_set_pixel(x + dx, y + dy, c.r, c.g, c.b);
                }
            }
        }
        s_st.hue_off = fmodf(s_st.hue_off + 15, 255);
    }
}

/**
 * FX_PUDDLEPEAK (17) - 水塘峰值
 */
void fx_puddlepeak(const mic_data_t* d, const settings_t* s) {
    fade_out(20 + s->speed / 8); // 较慢的衰减，保留美感

    // 改用基于音量的随机概率触发，解决持续声音不亮的问题
    float chance = d->volume * (s->intensity / 64.0f + 1.0f);
    
    // 如果音量足够大，且随机数击中概率，则产生一个水塘
    if (d->volume > 0.2f && (esp_random() % 100) < (int)(chance * 15)) {
        int   size = (int)(d->volume * 3) + 1;
        int   x    = esp_random() % W;
        int   y    = esp_random() % H;
        // 使用调色板颜色，增加随机偏移
        rgb_t c    = palette_color(s->palette, (uint8_t)esp_random());

        for (int dy = -size; dy <= size; dy++) {
            for (int dx = -size; dx <= size; dx++) {
                if (dx * dx + dy * dy <= size * size) {
                    led_set_pixel(x + dx, y + dy, c.r, c.g, c.b);
                }
            }
        }
    }
}

/**
 * FX_RIPPLEPEAK (19) - 涟漪峰值
 */
void fx_ripplepeak(const mic_data_t* d, const settings_t* s) {
    fade_out(40);

    // 基于音量的多重触发逻辑
    if (d->volume > 0.4f) {
        // 查找空闲的涟漪槽位
        for (int i = 0; i < MAX_RIPPLES; i++) {
            if (s_st.ripple[i].state < 0 && (esp_random() % 100 < 10)) {
                s_st.ripple[i].pos   = esp_random() % (W * H);
                s_st.ripple[i].color = (uint8_t)esp_random();
                s_st.ripple[i].state = 0;
                break; // 每帧最多新增一个，防止过密
            }
        }
    }

    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (s_st.ripple[i].state < 0) continue;

        int   idx   = (int)s_st.ripple[i].pos;
        int   cx    = idx % W;
        int   cy    = idx / W;
        rgb_t c     = palette_color(s->palette, s_st.ripple[i].color);
        int   r     = s_st.ripple[i].state;
        int   max_r = s->intensity / 16 + 2;

        // 绘制圆环
        for (float a = 0; a < 6.28f; a += 0.2f) {
            int px = cx + (int)(cosf(a) * r);
            int py = cy + (int)(sinf(a) * r);
            uint8_t bri = 255 - (r * 255 / max_r);
            led_set_pixel(px, py, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
        }

        s_st.ripple[i].state++;
        if (s_st.ripple[i].state > max_r) s_st.ripple[i].state = -1;
    }
}

/**
 * FX_DJLIGHT (24) - DJ灯光
 */
void fx_djlight(const mic_data_t* d, const settings_t* s) {
    fade_out(s->speed);
    int w = W, h = H;
    
    if (d->volume > 0.3f) {
        uint8_t r = (uint8_t)(d->bands[0] * 255);
        uint8_t g = (uint8_t)(d->bands[3] * 255);
        uint8_t b = (uint8_t)(d->bands[6] * 255);
        
        int x = esp_random() % w;
        for(int y=0; y<h; y++) led_set_pixel(x, y, r, g, b);
        int y = esp_random() % h;
        for(int x=0; x<w; x++) led_set_pixel(x, y, r, g, b);
    }
}

/**
 * FX_2DCENTERBARS (26) - 中心柱
 * 修复：去掉 h-1-y 反转，让柱子从底部向上长
 */
void fx_2dcenterbars(const mic_data_t* d, const settings_t* s) {
    fade_out(s->speed);
    int w = W, h = H;

    bool center_h = s->custom1 > 128;
    bool center_v = s->custom2 > 128;
    bool color_v  = s->custom3 > 128;

    int x_count = center_v ? w / 2 : w;

    for (int x = 0; x < x_count; x++) {
        int band = x * MIC_BANDS / x_count;
        if (band >= MIC_BANDS) band = MIC_BANDS - 1;

        int bar_height = (int)(d->bands[band] * h);
        // 柱子从底部向上长，y_start = 0（底部），y_end = bar_height
        int y_start = center_h ? (h - bar_height) / 2 : 0;

        for (int y = 0; y < h; y++) {
            uint16_t color_idx;
            if (color_v) {
                color_idx = center_h ? (uint16_t)(abs(y - h / 2) * 255 / (h / 2))
                                     : (uint16_t)(y * 255 / h);
            } else {
                color_idx = band * 17;
            }
            rgb_t c = palette_color(s->palette, (uint8_t)color_idx);

            uint8_t is_bar = (y >= y_start && y < y_start + bar_height) ? 1 : 0;
            if (is_bar) {
                if (center_v) {
                    led_set_pixel(w / 2 + x, y, c.r, c.g, c.b);
                    led_set_pixel(w / 2 - 1 - x, y, c.r, c.g, c.b);
                } else {
                    led_set_pixel(x, y, c.r, c.g, c.b);
                }
            }
        }
    }
}

/**
 * FX_BLURZ (27) - 模糊色块
 * 修复：改用音量触发，增加每次添加的像素数量和大小
 */
void fx_blurz(const mic_data_t* d, const settings_t* s) {
    led_blur2d(s->custom2);
    fade_out(s->speed);

    // 改用音量触发，而非节拍检测
    float trigger = d->volume * (s->intensity / 64.0f + 1.0f);
    
    if (d->volume > 0.15f && (esp_random() % 100) < (int)(trigger * 20)) {
        // 根据音量决定添加的像素数量
        int count = (int)(d->volume * 4) + 1;
        
        for (int i = 0; i < count; i++) {
            int     x        = esp_random() % W;
            int     y        = esp_random() % H;
            uint8_t band_idx = (s_st.frame + i) % MIC_BANDS;
            uint8_t color    = band_idx * 32 + (uint8_t)s_st.hue_off;
            rgb_t   c        = palette_color(s->palette, color);
            uint8_t bri      = (uint8_t)(d->bands[band_idx] * 255);
            if (bri < 50) bri = 50;  // 确保最小亮度
            
            // 添加一个小色块（不只是单个像素）
            int size = (int)(d->volume * 2) + 1;
            for (int dy = -size; dy <= size; dy++) {
                for (int dx = -size; dx <= size; dx++) {
                    if (dx * dx + dy * dy <= size * size) {
                        int px = x + dx, py = y + dy;
                        if (px >= 0 && px < W && py >= 0 && py < H) {
                            led_set_pixel(px, py, c.r * bri / 255, c.g * bri / 255, c.b * bri / 255);
                        }
                    }
                }
            }
        }
    }

    s_st.frame++;
    s_st.hue_off = fmodf(s_st.hue_off + 0.3f, 255);
}
