#include "effects_internal.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

static const char*   TAG      = "effects";
static volatile bool s_paused = false;

fx_state_t s_st;
uint8_t    s_mode = 0;

const effect_info_t EFFECT_INFO[EFFECT_COUNT] = {
    {"频谱柱",   "色彩模式", "显示峰值", "开启镜像"},
    {"频谱均衡", "消退速率", "显示峰值", ""},
    {"中心柱",   "水平居中", "垂直居中", "颜色方向"},
    {"频谱映射", "增益调节", "",         ""},
    {"瀑布流",   "颜色偏移", "消退速率", ""},
    {"重力计",   "下落速度", "峰值保持", ""},
    {"重力中心", "下落速度", "峰值保持", ""},
    {"重力偏心", "下落速度", "峰值保持", ""},
    {"重力频率", "下落速度", "峰值保持", ""},
    {"下落木板", "下落速度", "频段数量", ""},
    {"矩阵像素", "滚动速度", "亮度增益", ""},
    {"频率波",   "波动速度", "扩散强度", ""},
    {"像素波",   "波动速度", "亮度增益", ""},
    {"涟漪峰值", "涟漪数量", "触发阈值", ""},
    {"弹跳球",   "球体数量", "轨迹消退", ""},
    {"水塘峰值", "消退速率", "触发阈值", ""},
    {"水塘",     "消退速率", "闪烁大小", ""},
    {"频率像素", "消退速率", "像素数量", ""},
    {"频率映射", "消退速率", "",         ""},
    {"随机像素", "像素数量", "颜色偏移", ""},
    {"噪声火焰", "火焰速度", "亮度阈值", ""},
    {"等离子",   "相位速度", "亮度阈值", ""},
    {"极光",     "流动速度", "色彩跨度", ""},
    {"中间噪声", "消退速率", "灵敏度",   ""},
    {"噪声计",   "消退速率", "灵敏度",   ""},
    {"噪声移动", "移动速度", "频段数量", ""},
    {"模糊色块", "消退速率", "模糊强度", ""},
    {"DJ灯光",   "扫描速度", "闪烁时长", ""},
};

void draw_bar(int band, int height, rgb_t c, const settings_t* s) {
    int w = W, h = H;
    if (s->freq_dir == 0) {
        for (int i = 0; i < h; i++) {
            if (i < height) led_set_pixel(band, h - 1 - i, c.r, c.g, c.b);
            else led_set_pixel(band, h - 1 - i, 0, 0, 0);
        }
    } else {
        for (int i = 0; i < w; i++) {
            if (i < height) led_set_pixel(i, h - 1 - band, c.r, c.g, c.b);
            else led_set_pixel(i, h - 1 - band, 0, 0, 0);
        }
    }
}

void noise_setup(void) {
    if (s_st.noise_init) return;
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

float noise2d(float x, float y) {
    if (!s_st.noise_init) noise_setup();
    int   ix = (int)floorf(x) & 255;
    int   iy = (int)floorf(y) & 255;
    float fx = x - floorf(x);
    float fy = y - floorf(y);
    fx       = fx * fx * (3.0f - 2.0f * fx);
    fy       = fy * fy * (3.0f - 2.0f * fy);
    int   aa = s_st.perm[(s_st.perm[ix] + iy) & 255];
    int   ba = s_st.perm[(s_st.perm[(ix + 1) & 255] + iy) & 255];
    int   ab = s_st.perm[(s_st.perm[ix] + ((iy + 1) & 255)) & 255];
    int   bb = s_st.perm[(s_st.perm[(ix + 1) & 255] + ((iy + 1) & 255)) & 255];
    float x0 = aa + fx * (ba - aa);
    float x1 = ab + fx * (bb - ab);
    return (x0 + fy * (x1 - x0)) / 255.0f;
}

uint16_t noise16(uint32_t x, uint32_t y) {
    if (!s_st.noise_init) noise_setup();
    int ix = (x >> 8) & 255;
    int iy = (y >> 8) & 255;
    return s_st.perm[(s_st.perm[ix] + iy) & 255] << 8;
}

void fade_out(uint8_t rate) {
    led_fade_all(rate);
}

uint8_t freq_to_color(float freq) {
    if (freq < 60.0f) return 0;
    if (freq > 8000.0f) return 255;
    float log_freq = log10f(freq);
    float log_min  = 1.778f;
    float log_max  = 3.903f;
    return (uint8_t)((log_freq - log_min) * 255.0f / (log_max - log_min));
}

int freq_to_pos(float freq, int max_pos) {
    if (freq < 60.0f) return 0;
    if (freq > 8000.0f) return max_pos - 1;
    float log_freq = log10f(freq);
    float log_min  = 1.778f;
    float log_max  = 3.903f;
    return (int)((log_freq - log_min) * max_pos / (log_max - log_min));
}

typedef void (*fx_fn_t)(const mic_data_t*, const settings_t*);

static const fx_fn_t FX_TABLE[EFFECT_COUNT] = {
    fx_spectrum, fx_2dgeq, fx_2dcenterbars, fx_binmap, fx_waterfall,
    fx_gravimeter, fx_gravcenter, fx_gravcentric, fx_gravfreq, fx_2dfunkyplank,
    fx_matripix, fx_freqwave, fx_pixelwave, fx_ripplepeak, fx_juggles,
    fx_puddlepeak, fx_puddles, fx_freqpixels, fx_freqmap, fx_pixels,
    fx_noisefire, fx_plasmoid, fx_aurora, fx_midnoise, fx_noisemeter,
    fx_noisemove, fx_blurz, fx_djlight,
};

void effects_init(void) {
    memset(&s_st, 0, sizeof(s_st));
    for (int i = 0; i < MAX_RIPPLES; i++) {
        s_st.ripple[i].state = -1;
    }
    s_st.balls_init = false;
    noise_setup();
    ESP_LOGI(TAG, "effects init ok, count: %d", EFFECT_COUNT);
}

void effects_set_mode(uint8_t id) {
    if (id < EFFECT_COUNT) {
        s_mode = id;

        uint8_t perm_backup[256];
        memcpy(perm_backup, s_st.perm, 256);
        bool noise_init_backup = s_st.noise_init;

        memset(&s_st, 0, sizeof(s_st));

        memcpy(s_st.perm, perm_backup, 256);
        s_st.noise_init = noise_init_backup;

        for (int i = 0; i < MAX_RIPPLES; i++) {
            s_st.ripple[i].state = -1;
        }
        s_st.balls_init = false;
    }
}

void effects_update(const mic_data_t* data, const settings_t* s) {
    if (s_paused || s_mode >= EFFECT_COUNT) return;
    if (!s_st.noise_init) noise_setup();
    FX_TABLE[s_mode](data, s);
}

void effects_pause(void) {
    s_paused = true;
}

void effects_resume(void) {
    s_paused = false;
}