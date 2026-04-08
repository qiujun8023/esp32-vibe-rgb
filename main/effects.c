#include "effects.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "led.h"
#include "palettes.h"

static const char*   TAG      = "effects";
static volatile bool s_paused = false;

// 效果元数据（需与前端 ctrl.js 的 EFFECTS 数组对齐）
const effect_info_t EFFECT_INFO[EFFECT_COUNT] = {
    {"Spectrum", "色彩模式", "显示峰值", "开启镜像"},
    {"Waterfall", "色彩饱和度", "消退速率", ""},
    {"Gravimeter", "停留时间", "峰值亮度", ""},
    {"Funky Plank", "下降速率", "间距密度", ""},
    {"Scroll", "滚动方向", "", ""},
    {"CenterBars", "色彩分布", "", ""},
    {"GravFreq", "重力强度", "响应灵敏度", ""},
    {"Super Freq", "线条数量", "", ""},
    {"Ripple", "扩散速率", "最大涟漪数", ""},
    {"Juggles", "球体数量", "轨迹消退", ""},
    {"Blurz", "色块数量", "模糊强度", ""},
    {"DJ Light", "扫描频率", "闪烁时长", ""},
    {"Ripplepeak", "扩散速率", "触发门限", ""},
    {"Freqwave", "波动幅度", "", ""},
    {"Freqmap", "色彩饱和度", "", ""},
    {"Noisemove", "噪声缩放", "亮度调节", ""},
    {"Rocktaves", "八度数量", "", ""},
    {"Energy", "动态响应", "底噪亮度", "径向模式"},
    {"Plasma", "细节复杂度", "音频调制", ""},
    {"Swirl", "旋转速度", "音频调制", ""},
    {"Waverly", "波动数量", "波动振幅", ""},
    {"Fire", "冷却速率", "点燃几率", "音频调制"},
};

#define MAX_RIPPLES 5
#define MAX_BALLS   8
#define MAX_BLOBS   8
#define W           led_width()
#define H           led_height()

// 效果内部运行状态机
typedef struct {
    float    phase;    // 时间相位偏移
    float    hue_off;  // 色调偏移
    uint32_t frame;    // 帧计数器

    // Waterfall / Scroll 缓冲区
    uint8_t scroll_buf[LED_MAX_COUNT][3];

    // Gravimeter / Funky Plank 状态
    float grav_pos[16];
    float grav_vel[16];
    int   peak_hold[16];
    float plank_y[16];

    // Ripple / RipplePeak 状态
    struct {
        float   x, y, r, age;
        uint8_t ri, gi, bi;
    } rip[MAX_RIPPLES];
    int rip_n;

    // Juggles 弹球状态
    float   ball_x[MAX_BALLS], ball_y[MAX_BALLS];
    float   ball_vx[MAX_BALLS], ball_vy[MAX_BALLS];
    uint8_t ball_r[MAX_BALLS], ball_g[MAX_BALLS], ball_b[MAX_BALLS];

    // Blurz 色块状态
    struct {
        float   x, y, bright;
        uint8_t ri, gi, bi;
    } blob[MAX_BLOBS];

    // DJ Light 状态
    int dj_col, dj_row, dj_cnt;

    // Fire 火焰缓冲区 (增加预留空间防止越界)
    uint8_t fire[LED_MAX_COUNT + 64];

    // Noise 噪声生成表
    uint8_t perm[256];
    bool    noise_init;

    // 效果内部状态变量
    float last_beat;
    float last_peak;
    bool  juggles_init;
} fx_state_t;

static fx_state_t s_st;
static uint8_t    s_mode = 0;

/**
 * 2D 噪声函数
 */
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
    for (int i = 255; i > 0; i--) {
        uint32_t r   = esp_random();
        int      j   = r % (i + 1);
        uint8_t  t   = s_st.perm[i];
        s_st.perm[i] = s_st.perm[j];
        s_st.perm[j] = t;
    }
    s_st.noise_init = true;
}

/**
 * 频谱绘制辅助函数
 */
static void draw_bar(int band, int height, rgb_t c, const settings_t* s) {
    int h = H;
    for (int i = 0; i < h; i++) {
        if (s->freq_dir == 0) {
            // 列为频段，y=0 为底部
            led_set_pixel(band, i, i < height ? c.r : 2, i < height ? c.g : 2, i < height ? c.b : 4);
        } else {
            // 行为频段，x=0 为左侧
            led_set_pixel(i, band, i < height ? c.r : 2, i < height ? c.g : 2, i < height ? c.b : 4);
        }
    }
}

// 0: Spectrum - 经典频谱
// custom1=0: 按频带着色（每列固定色）
// custom1>0: 按高度渐变着色（音乐播放器风格，底部调色板低端→顶部高端）
// custom2>64: 显示峰值点
// custom3>128: 镜像模式
static void fx_spectrum(const mic_data_t* d, const settings_t* s) {
    int  h      = H;
    bool mirror = s->custom3 > 128;
    int  bands  = mirror ? MIC_BANDS / 2 : MIC_BANDS;

    for (int b = 0; b < bands; b++) {
        int bar = (int)(d->bands[b] * h + 0.5f);
        if (bar > h) bar = h;

        if (s->custom1 == 0) {
            // 经典模式：整条柱子使用频带对应的调色板颜色
            uint8_t pos = b * 255 / (MIC_BANDS - 1);
            rgb_t   c   = palette_color(s->palette, pos);
            draw_bar(b, bar, c, s);
        } else {
            // 音乐播放器模式：颜色随高度变化（底部→顶部 = 调色板低→高）
            for (int y = 0; y < h; y++) {
                rgb_t c;
                if (y < bar) {
                    uint8_t pos = (h > 1) ? (uint8_t)(y * 255 / (h - 1)) : 255;
                    c           = palette_color(s->palette, pos);
                } else {
                    c = (rgb_t){2, 2, 4};
                }
                if (s->freq_dir == 0)
                    led_set_pixel(b, y, c.r, c.g, c.b);
                else
                    led_set_pixel(y, b, c.r, c.g, c.b);
            }
        }

        // 峰值保持
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

    // 镜像：将左半侧复制到右半侧
    if (mirror && s->freq_dir == 0) {
        for (int b = 0; b < MIC_BANDS / 2; b++) {
            uint8_t r, g, bl;
            for (int y = 0; y < H; y++) {
                led_get_pixel(b, y, &r, &g, &bl);
                led_set_pixel(MIC_BANDS - 1 - b, y, r, g, bl);
            }
        }
    }
}

// 1: Waterfall - 瀑布流
static void fx_waterfall(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    if (w * h > LED_MAX_COUNT) return;

    for (int y = 0; y < h - 1; y++) {
        for (int x = 0; x < w; x++) {
            int dst                 = y * w + x;
            int src                 = (y + 1) * w + x;
            s_st.scroll_buf[dst][0] = s_st.scroll_buf[src][0];
            s_st.scroll_buf[dst][1] = s_st.scroll_buf[src][1];
            s_st.scroll_buf[dst][2] = s_st.scroll_buf[src][2];
            led_set_pixel(x, y, s_st.scroll_buf[dst][0], s_st.scroll_buf[dst][1], s_st.scroll_buf[dst][2]);
        }
    }

    uint8_t sat = 200 + s->custom1 / 18;
    for (int b = 0; b < MIC_BANDS && b < w; b++) {
        uint8_t v               = (uint8_t)(d->bands[b] * 255);
        rgb_t   c               = _hsv2rgb((uint16_t)(b * 360 / MIC_BANDS), sat, v);
        int     idx             = (h - 1) * w + b;
        s_st.scroll_buf[idx][0] = c.r;
        s_st.scroll_buf[idx][1] = c.g;
        s_st.scroll_buf[idx][2] = c.b;
        led_set_pixel(b, h - 1, c.r, c.g, c.b);
    }

    uint8_t fade = 2 + s->custom2 / 32;
    for (int i = 0; i < w * (h - 1); i++) {
        s_st.scroll_buf[i][0] = s_st.scroll_buf[i][0] > fade ? s_st.scroll_buf[i][0] - fade : 0;
        s_st.scroll_buf[i][1] = s_st.scroll_buf[i][1] > fade ? s_st.scroll_buf[i][1] - fade : 0;
        s_st.scroll_buf[i][2] = s_st.scroll_buf[i][2] > fade ? s_st.scroll_buf[i][2] - fade : 0;
    }
}

// 2: Gravimeter - 重力频谱
static void fx_gravimeter(const mic_data_t* d, const settings_t* s) {
    int   h       = H;
    float gravity = 0.15f + s->speed / 512.0f;
    for (int b = 0; b < MIC_BANDS; b++) {
        float target = d->bands[b] * h;
        if (target > s_st.grav_pos[b]) {
            s_st.grav_pos[b]  = target;
            s_st.grav_vel[b]  = 0;
            s_st.peak_hold[b] = s->custom1 / 4 + 8;
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
    int dir = s->custom1 > 128;
    if (!dir) {
        for (int x = 0; x < w - 1; x++)
            for (int y = 0; y < h; y++) {
                uint8_t r, g, b;
                led_get_pixel(x + 1, y, &r, &g, &b);
                led_set_pixel(x, y, r, g, b);
            }
    } else {
        for (int x = w - 1; x > 0; x--)
            for (int y = 0; y < h; y++) {
                uint8_t r, g, b;
                led_get_pixel(x - 1, y, &r, &g, &b);
                led_set_pixel(x, y, r, g, b);
            }
    }
    int nx = dir ? 0 : w - 1;
    for (int b = 0; b < MIC_BANDS && b < h; b++) {
        uint8_t v = (uint8_t)(d->bands[b] * 255);
        rgb_t   c = palette_color(s->palette, b * 255 / (MIC_BANDS - 1));
        led_set_pixel(nx, b, c.r * v / 255, c.g * v / 255, c.b * v / 255);
    }
}

// 5: 中心柱
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
        if (s_st.grav_pos[b] < 0) s_st.grav_pos[b] = 0;
        int y = (int)s_st.grav_pos[b];
        if (y >= h) y = h - 1;
        rgb_t c = palette_color(s->palette, b * 255 / (MIC_BANDS - 1));
        led_set_pixel(b < w ? b : w - 1, y, c.r, c.g, c.b);
    }
}

// 7: Super Freq
static void fx_superfreq(const mic_data_t* d, const settings_t* s) {
    led_fade_all(40 + s->speed / 8);
    int lines = 1 + s->custom1 / 43;
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

// 8: Ripple - 涟漪效果
static void fx_ripple(const mic_data_t* d, const settings_t* s) {
    led_fade_all(30);
    float speed  = 0.1f + s->custom1 / 512.0f;
    int   maxrip = 1 + s->custom2 / 52;
    if (d->beat > 0.5f && d->beat > s_st.last_beat + 0.3f) {
        if (s_st.rip_n < MAX_RIPPLES && s_st.rip_n < maxrip) {
            int i           = s_st.rip_n++;
            s_st.rip[i].x   = W / 2.0f;
            s_st.rip[i].y   = H / 2.0f;
            s_st.rip[i].r   = 0;
            s_st.rip[i].age = 0;
            rgb_t c         = palette_color(s->palette, (uint8_t)s_st.hue_off);
            s_st.rip[i].ri  = c.r;
            s_st.rip[i].gi  = c.g;
            s_st.rip[i].bi  = c.b;
            s_st.hue_off += 40;
        }
    }
    s_st.last_beat = d->beat;
    for (int i = 0; i < s_st.rip_n;) {
        s_st.rip[i].r += speed;
        float   rad = s_st.rip[i].r;
        uint8_t brt = rad > 0 ? (uint8_t)(200.0f / (rad * 0.5f + 1)) : 200;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                float dist =
                    sqrtf((x - s_st.rip[i].x) * (x - s_st.rip[i].x) + (y - s_st.rip[i].y) * (y - s_st.rip[i].y));
                if (fabsf(dist - rad) < 0.7f)
                    led_set_pixel(x, y, s_st.rip[i].ri * brt / 255, s_st.rip[i].gi * brt / 255,
                                  s_st.rip[i].bi * brt / 255);
            }
        if (rad > sqrtf(W * W + H * H) / 2.0f)
            s_st.rip[i] = s_st.rip[--s_st.rip_n];
        else
            i++;
    }
}

// 9: Juggles - 弹球效果
static void fx_juggles(const mic_data_t* d, const settings_t* s) {
    led_fade_all(20 + s->custom2 / 8);
    int balls = 1 + s->custom1 / 37;
    if (balls > MAX_BALLS) balls = MAX_BALLS;
    if (!s_st.juggles_init) {
        for (int i = 0; i < MAX_BALLS; i++) {
            s_st.ball_x[i]  = esp_random() % W;
            s_st.ball_y[i]  = esp_random() % H;
            s_st.ball_vx[i] = (esp_random() % 20 - 10) / 10.0f;
            s_st.ball_vy[i] = (esp_random() % 20 - 10) / 10.0f;
            rgb_t c         = palette_color(s->palette, i * 255 / MAX_BALLS);
            s_st.ball_r[i]  = c.r;
            s_st.ball_g[i]  = c.g;
            s_st.ball_b[i]  = c.b;
        }
        s_st.juggles_init = true;
    }
    float sm = 0.5f + d->beat * 1.5f + s->speed / 256.0f;
    for (int i = 0; i < balls; i++) {
        s_st.ball_x[i] += s_st.ball_vx[i] * sm;
        s_st.ball_y[i] += s_st.ball_vy[i] * sm;
        if (s_st.ball_x[i] < 0 || s_st.ball_x[i] >= W) s_st.ball_vx[i] *= -1;
        if (s_st.ball_y[i] < 0 || s_st.ball_y[i] >= H) s_st.ball_vy[i] *= -1;
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
            s_st.blob[i].x      = esp_random() % W;
            s_st.blob[i].y      = esp_random() % H;
            s_st.blob[i].bright = 255;
            rgb_t c             = palette_color(s->palette, esp_random() % 256);
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

// 11: DJ 灯光
static void fx_djlight(const mic_data_t* d, const settings_t* s) {
    int dur = 2 + s->custom2 / 64;
    if (d->beat > 0.5f && s_st.dj_cnt == 0) {
        s_st.dj_col = esp_random() % W;
        s_st.dj_row = esp_random() % H;
        s_st.dj_cnt = dur;
    }
    led_clear();
    if (s_st.dj_cnt > 0) {
        s_st.dj_cnt--;
        rgb_t c      = palette_color(s->palette, (uint8_t)s_st.hue_off);
        s_st.hue_off = fmodf(s_st.hue_off + s->custom1 / 4.0f, 255);
        for (int y = 0; y < H; y++) led_set_pixel(s_st.dj_col, y, c.r, c.g, c.b);
        for (int x = 0; x < W; x++) led_set_pixel(x, s_st.dj_row, c.r, c.g, c.b);
    }
}

// 12: Ripplepeak - 峰值涟漪
static void fx_ripplepeak(const mic_data_t* d, const settings_t* s) {
    led_fade_all(30);
    float speed = 0.08f + s->custom1 / 640.0f;
    float thr   = s->custom2 / 255.0f * 0.6f + 0.2f;
    if (d->peak > thr && d->peak > s_st.last_peak + 0.1f && s_st.rip_n < MAX_RIPPLES) {
        int i          = s_st.rip_n++;
        s_st.rip[i].x  = esp_random() % W;
        s_st.rip[i].y  = esp_random() % H;
        s_st.rip[i].r  = 0;
        rgb_t c        = palette_color(s->palette, (uint8_t)s_st.hue_off);
        s_st.rip[i].ri = c.r;
        s_st.rip[i].gi = c.g;
        s_st.rip[i].bi = c.b;
        s_st.hue_off   = fmodf(s_st.hue_off + 40, 255);
    }
    s_st.last_peak = d->peak;
    for (int i = 0; i < s_st.rip_n;) {
        s_st.rip[i].r += speed;
        float rad = s_st.rip[i].r;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                float dist =
                    sqrtf((x - s_st.rip[i].x) * (x - s_st.rip[i].x) + (y - s_st.rip[i].y) * (y - s_st.rip[i].y));
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
        int   b    = x * MIC_BANDS / W;
        float wave = sinf(s_st.phase + x * 0.8f) * amp * d->bands[b];
        int   cy   = H / 2 + (int)(wave * H / 4);
        for (int y = 0; y < H; y++) {
            float   dist = fabsf((float)(y - cy));
            uint8_t brt  = dist < 1.2f ? 255 : (dist < 2.5f ? 120 : 0);
            rgb_t   c    = palette_color(s->palette, b * 255 / (MIC_BANDS - 1));
            led_set_pixel(x, y, c.r * brt / 255, c.g * brt / 255, c.b * brt / 255);
        }
    }
}

// 14: Freqmap
static void fx_freqmap(const mic_data_t* d, const settings_t* s) {
    uint8_t sat = 128 + s->custom1 / 2;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int b = (x + y * W) * MIC_BANDS / (W * H);
            if (b >= MIC_BANDS) b = MIC_BANDS - 1;
            led_set_pixel_hsv(x, y, (uint16_t)(b * 360 / MIC_BANDS), sat, (uint8_t)(d->bands[b] * 255));
        }
}

// 15: Noisemove
static void fx_noisemove(const mic_data_t* d, const settings_t* s) {
    if (!s_st.noise_init) noise_setup();
    float sc  = 0.3f + s->custom1 / 512.0f;
    float mod = 0.5f + s->custom2 / 256.0f;
    s_st.phase += 0.02f + d->volume * 0.05f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float n = noise2d(x * sc + s_st.phase, y * sc);
            led_set_pixel_hsv(x, y, (uint16_t)(n * 360 + d->dominant_freq * 45) % 360, 220,
                              (uint8_t)(n * 255 * (0.3f + d->volume * mod)));
        }
}

// 16: Rocktaves
static void fx_rocktaves(const mic_data_t* d, const settings_t* s) {
    led_fade_all(40);
    int oct = 1 + s->custom1 / 52;
    if (oct > 6) oct = 6;
    for (int i = 0; i < oct && i < MIC_BANDS; i++) {
        if (d->bands[i] < 0.05f) continue;
        int   x = i * W / oct, y = (int)(d->bands[i] * (H - 1));
        rgb_t c = palette_color(s->palette, i * 255 / (oct));
        led_set_pixel(x, y, c.r, c.g, c.b);
        for (int h2 = 1; h2 < oct && x + h2 * W / oct < W; h2++) {
            led_set_pixel(x + h2 * W / oct, (int)(d->bands[i] / (h2 + 1) * (H - 1)), c.r / 2, c.g / 2, c.b / 2);
        }
    }
}

// 17: Energy
static void fx_energy(const mic_data_t* d, const settings_t* s) {
    s_st.hue_off  = fmodf(s_st.hue_off + 0.3f + d->dominant_freq * 0.5f, 360);
    uint8_t min_v = s->custom2 / 2, v = (uint8_t)(d->volume * (200 - min_v) + min_v + d->beat * 55);
    bool    radial = s->custom3 > 128;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float scale = 1.0f;
            if (radial) {
                float dx = x - (W - 1) / 2.0f, dy = y - (H - 1) / 2.0f;
                scale = 1.0f - sqrtf(dx * dx + dy * dy) / (W * 0.9f);
                if (scale < 0) scale = 0;
            }
            led_set_pixel_hsv(x, y, (uint16_t)s_st.hue_off, 200 + (uint8_t)(d->beat * 55),
                              (uint8_t)(v * scale + d->beat * 50));
        }
}

// 18: Plasma
static void fx_plasma(const mic_data_t* d, const settings_t* s) {
    float complex = 1.0f + s->custom1 / 64.0f;
    s_st.phase += 0.04f + d->volume * s->custom2 / 128.0f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float v = (sinf(x * complex * 0.4f + s_st.phase) + sinf(y * complex * 0.4f + s_st.phase * 0.7f) +
                       sinf((x + y) * complex * 0.3f + s_st.phase * 1.3f) +
                       sinf(sqrtf((float)(x * x + y * y)) * complex * 0.4f + s_st.phase) + 4) /
                      8.0f;
            rgb_t   c   = palette_color(s->palette, (uint8_t)(v * 255));
            uint8_t brt = (uint8_t)(100 + d->volume * 155);
            led_set_pixel(x, y, c.r * brt / 255, c.g * brt / 255, c.b * brt / 255);
        }
}

// 19: Swirl
static void fx_swirl(const mic_data_t* d, const settings_t* s) {
    s_st.phase += (0.02f + s->custom1 / 2048.0f) * (1 + d->volume * s->custom2 / 128.0f);
    float cx = W / 2.0f, cy = H / 2.0f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float   a = atan2f(y - cy, x - cx) + s_st.phase, dist = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
            rgb_t   c   = palette_color(s->palette, (uint8_t)((sinf(a * 3 + dist * 0.8f) + 1) / 2.0f * 255));
            uint8_t brt = (uint8_t)(80 + d->volume * 175);
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
        float off = w2 * H / waves;
        rgb_t c   = palette_color(s->palette, w2 * 255 / waves);
        for (int x = 0; x < W; x++) {
            int b = x * MIC_BANDS / W;
            int y = (int)(off + sinf(s_st.phase + x * 0.9f + w2) * amp * d->bands[b] * H / 2);
            if (y >= 0 && y < H) {
                led_set_pixel(x, y, c.r, c.g, c.b);
                if (y + 1 < H) led_set_pixel(x, y + 1, c.r / 2, c.g / 2, c.b / 2);
            }
        }
    }
}

// 21: Fire - 经典火焰效果
static void fx_fire(const mic_data_t* d, const settings_t* s) {
    int w = W, h = H;
    if (w * (h + 2) > LED_MAX_COUNT + 32) return;

    uint8_t cooling  = 120 - s->custom1 / 3;
    uint8_t sparking = 60 + s->custom2 / 3 + (uint8_t)(d->volume * s->custom3 / 2);
    if (sparking > 200) sparking = 200;

    for (int i = 0; i < w * (h + 2); i++) {
        int cool     = esp_random() % ((cooling * 10 / h) + 2);
        s_st.fire[i] = s_st.fire[i] > cool ? s_st.fire[i] - cool : 0;
    }
    for (int y = 0; y < h + 1; y++) {
        for (int x = 0; x < w; x++) {
            int i0 = y * w + x, i1 = (y + 1) * w + x, i2 = (y + 2) * w + x;
            s_st.fire[i0] = (s_st.fire[i0] + s_st.fire[i1] + s_st.fire[i1] + s_st.fire[i2]) / 4;
        }
    }
    if (esp_random() % 255 < sparking) {
        int x                      = esp_random() % w;
        s_st.fire[(h + 1) * w + x] = 160 + esp_random() % 95;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            rgb_t c = palette_color(PALETTE_HEAT, s_st.fire[y * w + x]);
            led_set_pixel(x, y, c.r, c.g, c.b);
        }
    }
}

// 效果函数分发表（需与 EFFECT_INFO 顺序保持一致）
typedef void (*fx_fn_t)(const mic_data_t*, const settings_t*);
static const fx_fn_t FX_TABLE[EFFECT_COUNT] = {
    fx_spectrum,  fx_waterfall, fx_gravimeter, fx_funky_plank, fx_scroll,     fx_centerbars, fx_gravfreq, fx_superfreq,
    fx_ripple,    fx_juggles,   fx_blurz,      fx_djlight,     fx_ripplepeak, fx_freqwave,   fx_freqmap,  fx_noisemove,
    fx_rocktaves, fx_energy,    fx_plasma,     fx_swirl,       fx_waverly,    fx_fire,
};

void effects_init(void) {
    memset(&s_st, 0, sizeof(s_st));
    noise_setup();
    ESP_LOGI(TAG, "effects engine initialized");
}

void effects_set_mode(uint8_t id) {
    if (id >= EFFECT_COUNT) id = 0;
    s_mode = id;

    // 切换模式时重置所有特效的内部缓冲区和状态
    memset(s_st.scroll_buf, 0, sizeof(s_st.scroll_buf));
    memset(s_st.fire, 0, sizeof(s_st.fire));
    s_st.rip_n        = 0;
    s_st.juggles_init = false;

    ESP_LOGI(TAG, "switched to effect: %d (%s)", s_mode, EFFECT_INFO[s_mode].name);
}

void effects_update(const mic_data_t* data, const settings_t* s) {
    if (s_paused) return;

    s_st.frame++;

    // 执行当前选中的效果函数
    if (FX_TABLE[s_mode]) {
        FX_TABLE[s_mode](data, s);
    }

    // 提交帧缓冲并刷新显示
    led_flush();
}

void effects_pause(void) {
    s_paused = true;
    ESP_LOGI(TAG, "effects paused");
}

void effects_resume(void) {
    s_paused = false;
    ESP_LOGI(TAG, "effects resumed");
}
