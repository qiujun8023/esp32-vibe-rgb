#include "effects.h"

#include <esp_timer.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "led.h"
#include "palettes.h"

static volatile bool s_paused = false;

// ── 效果元数据（与 ctrl.js 的 EFFECTS 数组顺序一致）───
const effect_info_t EFFECT_INFO[EFFECT_COUNT] = {
    /* 0*/ {"Spectrum", "色彩模式", "峰值点", "镜像"},
    /* 1*/ {"Waterfall", "饱和度", "消退速率", ""},
    /* 2*/ {"Gravimeter", "保持时间", "峰值亮度", ""},
    /* 3*/ {"Funky Plank", "下落速度", "间距", ""},
    /* 4*/ {"Scroll", "滚动方向", "", ""},
    /* 5*/ {"CenterBars", "色彩模式", "", ""},
    /* 6*/ {"GravFreq", "重力强度", "灵敏度", ""},
    /* 7*/ {"Super Freq", "线条数量", "", ""},
    /* 8*/ {"Ripple", "扩散速度", "最大涟漪", ""},
    /* 9*/ {"Juggles", "球数量", "消退速率", ""},
    /*10*/ {"Blurz", "色块数量", "模糊度", ""},
    /*11*/ {"DJ Light", "扫描速度", "闪光时长", ""},
    /*12*/ {"Ripplepeak", "扩散速度", "触发阈值", ""},
    /*13*/ {"Freqwave", "波动幅度", "", ""},
    /*14*/ {"Freqmap", "饱和度", "", ""},
    /*15*/ {"Noisemove", "噪声缩放", "亮度调制", ""},
    /*16*/ {"Rocktaves", "八度数量", "", ""},
    /*17*/ {"Energy", "响应速度", "最低亮度", "径向模式"},
    /*18*/ {"Plasma", "复杂度", "音频调制", ""},
    /*19*/ {"Swirl", "旋转速度", "音频调制", ""},
    /*20*/ {"Waverly", "波动数量", "振幅", ""},
    /*21*/ {"Fire", "冷却速率", "点燃率", "音频调制"},
};

// ── 全局效果状态（切换时清零）───────────────────────
#define MAX_RIPPLES 5
#define MAX_BALLS   8
#define MAX_BLOBS   8
#define W           led_width()
#define H           led_height()

typedef struct {
    // 通用
    float    phase;
    float    hue_off;
    uint32_t frame;

    // Waterfall / Scroll
    uint8_t scroll_buf[8][8][3];
    int     scroll_col;

    // Gravimeter / Funky Plank
    float grav_pos[8];
    float grav_vel[8];
    int   peak_hold[8];
    float plank_y[8];

    // Ripple / Ripplepeak
    struct {
        float   x, y, r, age;
        uint8_t ri, gi, bi;
    } rip[MAX_RIPPLES];
    int rip_n;

    // Juggles
    float   ball_x[MAX_BALLS], ball_y[MAX_BALLS];
    float   ball_vx[MAX_BALLS], ball_vy[MAX_BALLS];
    uint8_t ball_r[MAX_BALLS], ball_g[MAX_BALLS], ball_b[MAX_BALLS];

    // Blurz
    struct {
        float   x, y, bright;
        uint8_t ri, gi, bi;
    } blob[MAX_BLOBS];

    // DJ Light
    int dj_col, dj_row, dj_cnt;

    // Fire
    uint8_t fire[10][8];  // rows H+2

    // Noise
    uint8_t perm[256];
    bool    noise_init;
} fx_state_t;

static fx_state_t s_st;
static uint8_t    s_mode = 0;

// ── 噪声辅助 ─────────────────────────────────────────
static float noise2d(float x, float y) {
    int   ix = (int)floorf(x) & 255;
    int   iy = (int)floorf(y) & 255;
    float fx = x - floorf(x);
    float fy = y - floorf(y);
    fx       = fx * fx * (3 - 2 * fx);
    fy       = fy * fy * (3 - 2 * fy);
    int   aa = s_st.perm[(ix + s_st.perm[iy & 255]) & 255];
    int   ba = s_st.perm[(ix + 1 + s_st.perm[iy & 255]) & 255];
    int   ab = s_st.perm[(ix + s_st.perm[(iy + 1) & 255]) & 255];
    int   bb = s_st.perm[(ix + 1 + s_st.perm[(iy + 1) & 255]) & 255];
    float x0 = aa + fx * (ba - aa);
    float x1 = ab + fx * (bb - ab);
    return (x0 + fy * (x1 - x0)) / 255.0f;
}

static void noise_setup(void) {
    for (int i = 0; i < 256; i++) s_st.perm[i] = i;
    uint32_t seed = 54321;
    for (int i = 255; i > 0; i--) {
        seed         = seed * 1664525 + 1013904223;
        int     j    = (seed >> 16) % (i + 1);
        uint8_t t    = s_st.perm[i];
        s_st.perm[i] = s_st.perm[j];
        s_st.perm[j] = t;
    }
    s_st.noise_init = true;
}

// ── 频谱绘制辅助（freq_dir 感知）──────────────────────
// 在矩阵上绘制一根柱（band=列/行，height=0-H，c=颜色）
static void draw_bar(int band, int height, rgb_t c, const settings_t* s) {
    int w = W, h = H;
    for (int i = 0; i < h; i++) {
        if (s->freq_dir == 0) {
            // 列=频段，行=幅度，y=0 在底部
            led_set_pixel(band, i, i < height ? c.r : 2, i < height ? c.g : 2, i < height ? c.b : 4);
        } else {
            // 行=频段，列=幅度，x=0 在左
            led_set_pixel(i, band, i < height ? c.r : 2, i < height ? c.g : 2, i < height ? c.b : 4);
        }
    }
    (void)w;
}

// ── 效果实现 ─────────────────────────────────────────

// 0: Spectrum
static void fx_spectrum(const mic_data_t* d, const settings_t* s) {
    int h = H;
    for (int b = 0; b < MIC_BANDS; b++) {
        int bar = (int)(d->bands[b] * h + 0.5f);
        if (bar > h) bar = h;
        uint8_t pos = (s->custom1 == 0) ? (b * 255 / (MIC_BANDS - 1)) : (uint8_t)(d->bands[b] * 255);
        rgb_t   c   = palette_color(s->palette, pos);
        draw_bar(b, bar, c, s);

        // 峰值点
        if (s->custom2 > 64) {
            if (bar > s_st.peak_hold[b]) {
                s_st.peak_hold[b] = bar;
                s_st.grav_vel[b]  = 0;
            }
            if (s_st.grav_vel[b]++ > s->intensity / 16) {
                if (s_st.peak_hold[b] > 0) s_st.peak_hold[b]--;
                s_st.grav_vel[b] = 0;
            }
            int py = s_st.peak_hold[b];
            if (py > 0 && py < h) {
                if (s->freq_dir == 0)
                    led_set_pixel_hsv(b, py, b * 30, 80, 255);
                else
                    led_set_pixel_hsv(py, b, b * 30, 80, 255);
            }
        }
    }
    // 镜像
    if (s->custom3 > 128) {
        for (int b = 0; b < MIC_BANDS / 2; b++) {
            uint8_t r1, g1, b1, r2, g2, b2;
            if (s->freq_dir == 0) {
                led_get_pixel(b, 0, &r1, &g1, &b1);
                led_get_pixel(MIC_BANDS - 1 - b, 0, &r2, &g2, &b2);
                for (int y = 0; y < H; y++) {
                    led_get_pixel(b, y, &r1, &g1, &b1);
                    led_set_pixel(MIC_BANDS - 1 - b, y, r1, g1, b1);
                }
            }
        }
    }
}

// 1: Waterfall
static void fx_waterfall(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    // 向下移动一行
    for (int y = 0; y < h - 1; y++)
        for (int x = 0; x < w; x++) {
            led_set_pixel(x, y, s_st.scroll_buf[y + 1][x][0], s_st.scroll_buf[y + 1][x][1],
                          s_st.scroll_buf[y + 1][x][2]);
            s_st.scroll_buf[y][x][0] = s_st.scroll_buf[y + 1][x][0];
            s_st.scroll_buf[y][x][1] = s_st.scroll_buf[y + 1][x][1];
            s_st.scroll_buf[y][x][2] = s_st.scroll_buf[y + 1][x][2];
        }
    // 顶行写入当前频谱
    for (int b = 0; b < MIC_BANDS && b < w; b++) {
        uint8_t pos = (uint8_t)(d->bands[b] * 255);
        rgb_t   c   = palette_color(s->palette, pos);
        uint8_t sat = 200 + s->custom1 / 18;
        // 用频段幅度控制亮度
        uint8_t v = (uint8_t)(d->bands[b] * 255);
        c         = _hsv2rgb((uint16_t)(b * 360 / MIC_BANDS), sat, v);
        led_set_pixel(b, h - 1, c.r, c.g, c.b);
        s_st.scroll_buf[h - 1][b][0] = c.r;
        s_st.scroll_buf[h - 1][b][1] = c.g;
        s_st.scroll_buf[h - 1][b][2] = c.b;
    }
    // 消退（fade）
    uint8_t fade = 2 + s->custom2 / 32;
    for (int y = 0; y < h - 1; y++)
        for (int x = 0; x < w; x++) {
            uint8_t r                = s_st.scroll_buf[y][x][0];
            uint8_t g                = s_st.scroll_buf[y][x][1];
            uint8_t b2               = s_st.scroll_buf[y][x][2];
            r                        = r > fade ? r - fade : 0;
            g                        = g > fade ? g - fade : 0;
            b2                       = b2 > fade ? b2 - fade : 0;
            s_st.scroll_buf[y][x][0] = r;
            s_st.scroll_buf[y][x][1] = g;
            s_st.scroll_buf[y][x][2] = b2;
        }
}

// 2: Gravimeter
static void fx_gravimeter(const mic_data_t* d, const settings_t* s) {
    int   h       = H;
    float gravity = 0.15f + s->speed / 512.0f;
    for (int b = 0; b < MIC_BANDS; b++) {
        float target = d->bands[b] * h;
        if (target > s_st.grav_pos[b]) {
            s_st.grav_pos[b]  = target;
            s_st.grav_vel[b]  = 0;
            s_st.peak_hold[b] = s->custom1 / 4 + 8;  // hold frames
        } else {
            if (s_st.peak_hold[b] > 0)
                s_st.peak_hold[b]--;
            else {
                s_st.grav_vel[b] += gravity;
                s_st.grav_pos[b] -= s_st.grav_vel[b];
            }
        }
        if (s_st.grav_pos[b] < 0) {
            s_st.grav_pos[b] = 0;
            s_st.grav_vel[b] = 0;
        }

        int     bar     = (int)s_st.grav_pos[b];
        uint8_t col_pos = b * 255 / (MIC_BANDS - 1);
        rgb_t   c       = palette_color(s->palette, col_pos);
        for (int y = 0; y < h; y++) {
            uint8_t bright = (y <= bar) ? 200 : 0;
            rgb_t   cp     = {c.r * bright / 200, c.g * bright / 200, c.b * bright / 200};
            if (s->freq_dir == 0)
                led_set_pixel(b, y, cp.r, cp.g, cp.b);
            else
                led_set_pixel(y, b, cp.r, cp.g, cp.b);
        }
        // 峰值亮点
        if (bar >= 0 && bar < h) {
            uint8_t v = 128 + s->custom2 / 2;
            if (s->freq_dir == 0)
                led_set_pixel_hsv(b, bar, b * 30, 80, v);
            else
                led_set_pixel_hsv(bar, b, b * 30, 80, v);
        }
    }
}

// 3: Funky Plank
static void fx_funky_plank(const mic_data_t* d, const settings_t* s) {
    led_fade_all(8 + s->speed / 16);
    int   h    = H;
    float fall = 0.05f + s->custom1 / 2048.0f;
    for (int b = 0; b < MIC_BANDS; b++) {
        if (d->bands[b] > 0.3f && s_st.plank_y[b] < 0.1f) s_st.plank_y[b] = (float)h;
        s_st.plank_y[b] -= fall;
        if (s_st.plank_y[b] < 0) s_st.plank_y[b] = 0;

        int y = (int)s_st.plank_y[b];
        if (y < h) {
            rgb_t c = palette_color(s->palette, b * 255 / (MIC_BANDS - 1));
            if (s->freq_dir == 0)
                led_set_pixel(b, y, c.r, c.g, c.b);
            else
                led_set_pixel(y, b, c.r, c.g, c.b);
        }
    }
}

// 4: Scroll
static void fx_scroll(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    // 向左移动一列
    int dir = s->custom1 > 128;  // 0=右移 1=左移
    if (!dir) {
        for (int x = 0; x < w - 1; x++)
            for (int y = 0; y < h; y++) {
                uint8_t r, g, b2;
                led_get_pixel(x + 1, y, &r, &g, &b2);
                led_set_pixel(x, y, r, g, b2);
            }
    } else {
        for (int x = w - 1; x > 0; x--)
            for (int y = 0; y < h; y++) {
                uint8_t r, g, b2;
                led_get_pixel(x - 1, y, &r, &g, &b2);
                led_set_pixel(x, y, r, g, b2);
            }
    }
    // 新列写入频谱
    int nx = dir ? 0 : w - 1;
    for (int b = 0; b < MIC_BANDS && b < h; b++) {
        uint8_t v = (uint8_t)(d->bands[b] * 255);
        rgb_t   c = palette_color(s->palette, b * 255 / (MIC_BANDS - 1));
        c.r       = c.r * v / 255;
        c.g       = c.g * v / 255;
        c.b       = c.b * v / 255;
        led_set_pixel(nx, b, c.r, c.g, c.b);
    }
    (void)h;
}

// 5: CenterBars
static void fx_centerbars(const mic_data_t* d, const settings_t* s) {
    int h = H, half = h / 2;
    for (int b = 0; b < MIC_BANDS; b++) {
        int     bar = (int)(d->bands[b] * half);
        uint8_t pos = (s->custom1 == 0) ? b * 255 / (MIC_BANDS - 1) : (uint8_t)(d->bands[b] * 255);
        rgb_t   c   = palette_color(s->palette, pos);
        for (int y = 0; y < h; y++) {
            int dist = abs(y - (h / 2 - 1));
            int lit  = dist < bar;
            if (s->freq_dir == 0)
                led_set_pixel(b, y, lit ? c.r : 2, lit ? c.g : 2, lit ? c.b : 4);
            else
                led_set_pixel(y, b, lit ? c.r : 2, lit ? c.g : 2, lit ? c.b : 4);
        }
    }
}

// 6: GravFreq
static void fx_gravfreq(const mic_data_t* d, const settings_t* s) {
    int h = H, w = W;
    led_fade_all(30);
    float gravity = 0.05f + s->custom1 / 2048.0f;
    float sens    = 0.5f + s->custom2 / 128.0f;
    for (int b = 0; b < MIC_BANDS; b++) {
        if (d->bands[b] * sens > s_st.grav_pos[b]) {
            s_st.grav_pos[b] = d->bands[b] * sens * h;
            s_st.grav_vel[b] = 0;
        } else {
            s_st.grav_vel[b] += gravity;
            s_st.grav_pos[b] -= s_st.grav_vel[b];
        }
        if (s_st.grav_pos[b] < 0) {
            s_st.grav_pos[b] = 0;
            s_st.grav_vel[b] = 0;
        }
        int y = (int)s_st.grav_pos[b];
        if (y >= h) y = h - 1;
        rgb_t c = palette_color(s->palette, b * 255 / (MIC_BANDS - 1));
        led_set_pixel(b < w ? b : w - 1, y < h ? y : h - 1, c.r, c.g, c.b);
    }
}

// 7: Super Freq
static void fx_superfreq(const mic_data_t* d, const settings_t* s) {
    led_fade_all(40 + s->speed / 8);
    int lines = 1 + s->custom1 / 43;  // 1-6 lines
    int h = H, w = W;
    for (int b = 0; b < MIC_BANDS; b++) {
        uint8_t v = (uint8_t)(d->bands[b] * 255);
        if (v < 20) continue;
        rgb_t c = palette_color(s->palette, b * 255 / (MIC_BANDS - 1));
        for (int l = 0; l < lines; l++) {
            int y = (l * h / lines + (int)(s_st.phase * 0.5f)) % h;
            led_set_pixel(b < w ? b : w - 1, y, c.r * v / 255, c.g * v / 255, c.b * v / 255);
        }
    }
    s_st.phase += 0.5f + d->volume;
}

// 8: Ripple
static void fx_ripple(const mic_data_t* d, const settings_t* s) {
    led_fade_all(30);
    float speed  = 0.1f + s->custom1 / 512.0f;
    int   maxrip = 1 + s->custom2 / 52;
    if (maxrip > MAX_RIPPLES) maxrip = MAX_RIPPLES;

    // 检测节拍触发
    static float last_beat = 0;
    if (d->beat > 0.5f && d->beat > last_beat + 0.3f) {
        if (s_st.rip_n < maxrip) {
            int i           = s_st.rip_n++;
            s_st.rip[i].x   = W / 2.0f;
            s_st.rip[i].y   = H / 2.0f;
            s_st.rip[i].r   = 0;
            s_st.rip[i].age = 0;
            rgb_t c         = palette_color(s->palette, (uint8_t)(s_st.hue_off));
            s_st.rip[i].ri  = c.r;
            s_st.rip[i].gi  = c.g;
            s_st.rip[i].bi  = c.b;
            s_st.hue_off += 40;
        }
    }
    last_beat = d->beat;

    // 扩散
    for (int i = 0; i < s_st.rip_n;) {
        s_st.rip[i].r += speed;
        s_st.rip[i].age += 1;
        float   rad = s_st.rip[i].r;
        float   cx = s_st.rip[i].x, cy = s_st.rip[i].y;
        uint8_t brt = rad > 0 ? (uint8_t)(200.0f / (rad * 0.5f + 1)) : 200;

        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                float dist = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
                if (fabsf(dist - rad) < 0.7f)
                    led_set_pixel(x, y, s_st.rip[i].ri * brt / 255, s_st.rip[i].gi * brt / 255,
                                  s_st.rip[i].bi * brt / 255);
            }

        if (s_st.rip[i].r > sqrtf(W * W + H * H) / 2.0f) {
            s_st.rip[i] = s_st.rip[--s_st.rip_n];
        } else
            i++;
    }
}

// 9: Juggles
static void fx_juggles(const mic_data_t* d, const settings_t* s) {
    led_fade_all(20 + s->custom2 / 8);
    int balls = 1 + s->custom1 / 37;
    if (balls > MAX_BALLS) balls = MAX_BALLS;

    // 初始化球
    static bool jinit = false;
    if (!jinit) {
        for (int i = 0; i < MAX_BALLS; i++) {
            s_st.ball_x[i]  = rand() % W;
            s_st.ball_y[i]  = rand() % H;
            s_st.ball_vx[i] = (rand() % 20 - 10) / 10.0f;
            s_st.ball_vy[i] = (rand() % 20 - 10) / 10.0f;
            rgb_t c         = palette_color(s->palette, i * 255 / MAX_BALLS);
            s_st.ball_r[i]  = c.r;
            s_st.ball_g[i]  = c.g;
            s_st.ball_b[i]  = c.b;
        }
        jinit = true;
    }

    float speed_mult = 0.5f + d->beat * 1.5f + s->speed / 256.0f;
    for (int i = 0; i < balls; i++) {
        s_st.ball_x[i] += s_st.ball_vx[i] * speed_mult;
        s_st.ball_y[i] += s_st.ball_vy[i] * speed_mult;
        if (s_st.ball_x[i] < 0 || s_st.ball_x[i] >= W) s_st.ball_vx[i] *= -1;
        if (s_st.ball_y[i] < 0 || s_st.ball_y[i] >= H) s_st.ball_vy[i] *= -1;
        s_st.ball_x[i] = s_st.ball_x[i] < 0 ? 0 : s_st.ball_x[i] >= W ? W - 1 : s_st.ball_x[i];
        s_st.ball_y[i] = s_st.ball_y[i] < 0 ? 0 : s_st.ball_y[i] >= H ? H - 1 : s_st.ball_y[i];
        led_set_pixel((int)s_st.ball_x[i], (int)s_st.ball_y[i], s_st.ball_r[i], s_st.ball_g[i], s_st.ball_b[i]);
    }
}

// 10: Blurz
static void fx_blurz(const mic_data_t* d, const settings_t* s) {
    led_blur2d(80 + s->custom2 / 3);
    int blobs = 1 + s->custom1 / 37;
    if (blobs > MAX_BLOBS) blobs = MAX_BLOBS;

    if (d->beat > 0.4f) {
        for (int i = 0; i < blobs; i++) {
            s_st.blob[i].x      = rand() % W;
            s_st.blob[i].y      = rand() % H;
            s_st.blob[i].bright = 255;
            rgb_t c             = palette_color(s->palette, rand() % 256);
            s_st.blob[i].ri     = c.r;
            s_st.blob[i].gi     = c.g;
            s_st.blob[i].bi     = c.b;
        }
    }
    for (int i = 0; i < blobs; i++) {
        uint8_t v = (uint8_t)s_st.blob[i].bright;
        led_set_pixel((int)s_st.blob[i].x, (int)s_st.blob[i].y, s_st.blob[i].ri * v / 255, s_st.blob[i].gi * v / 255,
                      s_st.blob[i].bi * v / 255);
        s_st.blob[i].bright *= 0.9f;
    }
}

// 11: DJ Light
static void fx_djlight(const mic_data_t* d, const settings_t* s) {
    int flash_dur = 2 + s->custom2 / 64;
    if (d->beat > 0.5f && s_st.dj_cnt == 0) {
        s_st.dj_col = rand() % W;
        s_st.dj_row = rand() % H;
        s_st.dj_cnt = flash_dur;
    }

    led_clear();
    if (s_st.dj_cnt > 0) {
        s_st.dj_cnt--;
        rgb_t c      = palette_color(s->palette, (uint8_t)s_st.hue_off);
        s_st.hue_off = fmodf(s_st.hue_off + s->custom1 / 4.0f, 255);
        // 扫列
        for (int y = 0; y < H; y++) led_set_pixel(s_st.dj_col, y, c.r, c.g, c.b);
        // 扫行
        for (int x = 0; x < W; x++) led_set_pixel(x, s_st.dj_row, c.r, c.g, c.b);
    }
}

// 12: Ripplepeak
static void fx_ripplepeak(const mic_data_t* d, const settings_t* s) {
    led_fade_all(30);
    float speed  = 0.08f + s->custom1 / 640.0f;
    float thr    = s->custom2 / 255.0f * 0.6f + 0.2f;
    int   maxrip = MAX_RIPPLES;

    static float last_peak = 0;
    if (d->peak > thr && d->peak > last_peak + 0.1f && s_st.rip_n < maxrip) {
        int i          = s_st.rip_n++;
        s_st.rip[i].x  = rand() % W;
        s_st.rip[i].y  = rand() % H;
        s_st.rip[i].r  = 0;
        rgb_t c        = palette_color(s->palette, (uint8_t)(s_st.hue_off));
        s_st.rip[i].ri = c.r;
        s_st.rip[i].gi = c.g;
        s_st.rip[i].bi = c.b;
        s_st.hue_off   = fmodf(s_st.hue_off + 40, 255);
    }
    last_peak = d->peak;

    for (int i = 0; i < s_st.rip_n;) {
        s_st.rip[i].r += speed;
        float rad = s_st.rip[i].r;
        float cx = s_st.rip[i].x, cy = s_st.rip[i].y;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                float dist = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
                if (fabsf(dist - rad) < 0.8f) {
                    uint8_t brt = (uint8_t)(200.0f / (rad * 0.3f + 1));
                    led_set_pixel(x, y, s_st.rip[i].ri * brt / 255, s_st.rip[i].gi * brt / 255,
                                  s_st.rip[i].bi * brt / 255);
                }
            }
        if (rad > sqrtf(W * W + H * H))
            s_st.rip[i] = s_st.rip[--s_st.rip_n];
        else
            i++;
    }
}

// 13: Freqwave
static void fx_freqwave(const mic_data_t* d, const settings_t* s) {
    float amp = 1.0f + s->custom1 / 64.0f;
    s_st.phase += 0.05f + d->volume * 0.1f;
    for (int x = 0; x < W; x++) {
        int   band  = x * MIC_BANDS / W;
        float bandv = d->bands[band];
        float wave  = sinf(s_st.phase + x * 0.8f) * amp * bandv;
        int   cy    = H / 2 + (int)(wave * H / 4);
        for (int y = 0; y < H; y++) {
            float   dist = fabsf((float)(y - cy));
            uint8_t brt  = dist < 1.2f ? 255 : (dist < 2.5f ? 120 : 0);
            rgb_t   c    = palette_color(s->palette, band * 255 / (MIC_BANDS - 1));
            led_set_pixel(x, y, c.r * brt / 255, c.g * brt / 255, c.b * brt / 255);
        }
    }
}

// 14: Freqmap
static void fx_freqmap(const mic_data_t* d, const settings_t* s) {
    uint8_t sat = 128 + s->custom1 / 2;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int      band = (x + y * W) * MIC_BANDS / (W * H);
            float    val  = d->bands[band < MIC_BANDS ? band : MIC_BANDS - 1];
            uint8_t  v    = (uint8_t)(val * 255);
            uint16_t hue  = (uint16_t)(band * 360 / MIC_BANDS);
            led_set_pixel_hsv(x, y, hue, sat, v);
        }
    }
}

// 15: Noisemove
static void fx_noisemove(const mic_data_t* d, const settings_t* s) {
    if (!s_st.noise_init) noise_setup();
    float scale = 0.3f + s->custom1 / 512.0f;
    float mod   = 0.5f + s->custom2 / 256.0f;
    s_st.phase += 0.02f + d->volume * 0.05f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float    n   = noise2d(x * scale + s_st.phase, y * scale);
            uint8_t  v   = (uint8_t)(n * 255 * (0.3f + d->volume * mod));
            uint16_t hue = (uint16_t)(n * 360 + d->dominant_freq * 45);
            led_set_pixel_hsv(x, y, hue % 360, 220, v);
        }
}

// 16: Rocktaves
static void fx_rocktaves(const mic_data_t* d, const settings_t* s) {
    led_fade_all(40);
    int octaves = 1 + s->custom1 / 52;
    if (octaves > 6) octaves = 6;
    int h = H, w = W;
    for (int oct = 0; oct < octaves && oct < MIC_BANDS; oct++) {
        float amp = d->bands[oct];
        if (amp < 0.05f) continue;
        int   x = oct * w / octaves;
        int   y = (int)(amp * (h - 1));
        rgb_t c = palette_color(s->palette, oct * 255 / (octaves - 1 + 1));
        led_set_pixel(x, y, c.r, c.g, c.b);
        // harmonics
        for (int h2 = 1; h2 < octaves && x + h2 * w / octaves < w; h2++) {
            float harm_amp = amp / (h2 + 1);
            int   hx       = x + h2 * w / octaves;
            int   hy       = (int)(harm_amp * (h - 1));
            led_set_pixel(hx, hy, c.r / 2, c.g / 2, c.b / 2);
        }
    }
}

// 17: Energy
static void fx_energy(const mic_data_t* d, const settings_t* s) {
    s_st.hue_off += 0.3f + d->dominant_freq * 0.5f;
    if (s_st.hue_off >= 360) s_st.hue_off -= 360;

    float   speed_f = 0.5f + s->custom1 / 255.0f;
    uint8_t min_v   = s->custom2 / 2;
    uint8_t v       = (uint8_t)(d->volume * (200 - min_v) + min_v + d->beat * 55);
    uint8_t sat     = 200 + (uint8_t)(d->beat * 55);
    bool    radial  = s->custom3 > 128;

    (void)speed_f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float scale = 1.0f;
            if (radial) {
                float dx = x - (W - 1) / 2.0f, dy = y - (H - 1) / 2.0f;
                scale = 1.0f - sqrtf(dx * dx + dy * dy) / (W * 0.9f);
                if (scale < 0) scale = 0;
            }
            uint8_t bv = (uint8_t)(v * scale + d->beat * 50);
            led_set_pixel_hsv(x, y, (uint16_t)s_st.hue_off, sat, bv);
        }
}

// 18: Plasma
static void fx_plasma(const mic_data_t* d, const settings_t* s) {
    float complexity = 1.0f + s->custom1 / 64.0f;
    float audio_mod  = s->custom2 / 128.0f;
    s_st.phase += 0.04f + d->volume * audio_mod;

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float   v1  = sinf(x * complexity * 0.4f + s_st.phase);
            float   v2  = sinf(y * complexity * 0.4f + s_st.phase * 0.7f);
            float   v3  = sinf((x + y) * complexity * 0.3f + s_st.phase * 1.3f);
            float   v4  = sinf(sqrtf((float)(x * x + y * y)) * complexity * 0.4f + s_st.phase);
            float   val = (v1 + v2 + v3 + v4 + 4) / 8.0f;  // 0-1
            uint8_t pos = (uint8_t)(val * 255);
            uint8_t brt = (uint8_t)(100 + d->volume * 155);
            rgb_t   c   = palette_color(s->palette, pos);
            led_set_pixel(x, y, c.r * brt / 255, c.g * brt / 255, c.b * brt / 255);
        }
}

// 19: Swirl
static void fx_swirl(const mic_data_t* d, const settings_t* s) {
    float rot_speed = (0.02f + s->custom1 / 2048.0f) * (1 + d->volume * s->custom2 / 128.0f);
    s_st.phase += rot_speed;
    float cx = W / 2.0f, cy = H / 2.0f;

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float   dx = x - cx, dy = y - cy;
            float   angle = atan2f(dy, dx) + s_st.phase;
            float   dist  = sqrtf(dx * dx + dy * dy);
            float   val   = (sinf(angle * 3 + dist * 0.8f) + 1) / 2.0f;
            uint8_t pos   = (uint8_t)(val * 255);
            uint8_t brt   = (uint8_t)(80 + d->volume * 175);
            rgb_t   c     = palette_color(s->palette, pos);
            led_set_pixel(x, y, c.r * brt / 255, c.g * brt / 255, c.b * brt / 255);
        }
}

// 20: Waverly
static void fx_waverly(const mic_data_t* d, const settings_t* s) {
    int   waves = 1 + s->custom1 / 52;
    float amp   = 0.5f + s->custom2 / 128.0f;
    s_st.phase += 0.05f + d->volume * 0.1f;
    led_clear();
    for (int w2 = 0; w2 < waves; w2++) {
        float offset = w2 * H / waves;
        rgb_t c      = palette_color(s->palette, w2 * 255 / waves);
        for (int x = 0; x < W; x++) {
            int   band  = x * MIC_BANDS / W;
            float bandv = d->bands[band < MIC_BANDS ? band : MIC_BANDS - 1];
            float y_f   = offset + sinf(s_st.phase + x * 0.9f + w2) * amp * bandv * H / 2;
            int   y     = (int)y_f;
            if (y >= 0 && y < H) {
                led_set_pixel(x, y, c.r, c.g, c.b);
                if (y + 1 < H) led_set_pixel(x, y + 1, c.r / 2, c.g / 2, c.b / 2);
            }
        }
    }
}

// 21: Fire
static void fx_fire(const mic_data_t* d, const settings_t* s) {
    int     w = W, h = H;
    uint8_t cooling  = 120 - s->custom1 / 3;  // speed控制冷却，越快越凉
    uint8_t sparking = 60 + s->custom2 / 3 + (uint8_t)(d->volume * s->custom3 / 2);
    if (sparking > 200) sparking = 200;

    // 冷却
    for (int y = 0; y < h + 2; y++)
        for (int x = 0; x < w; x++) {
            int cool        = rand() % ((cooling * 10 / h) + 2);
            s_st.fire[y][x] = s_st.fire[y][x] > cool ? s_st.fire[y][x] - cool : 0;
        }
    // 向上传播
    for (int y = 0; y < h + 1; y++)
        for (int x = 0; x < w; x++)
            s_st.fire[y][x] = (s_st.fire[y][x] + s_st.fire[y + 1][x] + s_st.fire[y + 1][x] + s_st.fire[y + 2][x]) / 4;
    // 底部点火
    if (rand() % 255 < sparking) {
        int x               = rand() % w;
        s_st.fire[h + 1][x] = 160 + rand() % 95;
    }
    // 渲染（y=0 在底部）
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            rgb_t c = palette_color(PALETTE_HEAT, s_st.fire[y][x]);
            led_set_pixel(x, y, c.r, c.g, c.b);
        }
}

// ── 分发表 ───────────────────────────────────────────
typedef void (*fx_fn_t)(const mic_data_t*, const settings_t*);
static const fx_fn_t FX_TABLE[EFFECT_COUNT] = {
    fx_spectrum,  fx_waterfall, fx_gravimeter, fx_funky_plank, fx_scroll,     fx_centerbars, fx_gravfreq, fx_superfreq,
    fx_ripple,    fx_juggles,   fx_blurz,      fx_djlight,     fx_ripplepeak, fx_freqwave,   fx_freqmap,  fx_noisemove,
    fx_rocktaves, fx_energy,    fx_plasma,     fx_swirl,       fx_waverly,    fx_fire,
};

// ── 公共接口 ─────────────────────────────────────────
void effects_init(void) {
    memset(&s_st, 0, sizeof(s_st));
}

void effects_set_mode(uint8_t id) {
    s_mode = id < EFFECT_COUNT ? id : 0;
    memset(&s_st, 0, sizeof(s_st));
}

void effects_update(const mic_data_t* data, const settings_t* s) {
    if (s_paused) return;
    s_st.frame++;
    if (FX_TABLE[s_mode]) FX_TABLE[s_mode](data, s);
    led_flush();
}

void effects_pause(void) {
    s_paused = true;
}
void effects_resume(void) {
    s_paused = false;
}
