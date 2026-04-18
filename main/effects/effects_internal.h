/* 仅供 effects 模块内部使用,外部请用 effects.h */

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "effects.h"
#include "led.h"
#include "mic.h"
#include "palettes.h"
#include "settings.h"

#define MAX_RIPPLES 16
#define MAX_BALLS   12
/* 列状态数组容量上限,矩阵宽度超过时按此截断 */
#define MAX_GRAV_COLS 64

#define W led_width()
#define H led_height()

static inline int get_idx(int x, int y) {
    if (x < 0 || x >= led_width() || y < 0 || y >= led_height()) return -1;
    return y * led_width() + x;
}

typedef struct {
    float    phase;
    float    hue_off;
    uint32_t frame;

    uint8_t scroll_buf[LED_MAX_COUNT][3];

    float grav_pos[MAX_GRAV_COLS];
    float grav_vel[MAX_GRAV_COLS];
    float peak_hold[MAX_GRAV_COLS];
    int   top_led[MAX_GRAV_COLS];
    int   geq_peak[MAX_GRAV_COLS];

    struct {
        float   pos;
        uint8_t color;
        int8_t  state;
    } ripple[MAX_RIPPLES];

    float   ball_x[MAX_BALLS], ball_y[MAX_BALLS];
    float   ball_vx[MAX_BALLS], ball_vy[MAX_BALLS];
    uint8_t ball_hue[MAX_BALLS];
    bool    balls_init;

    int dj_col, dj_row, dj_cnt;

    float last_beat;
    float last_peak;
    float last_vol;

    uint8_t perm[256];
    bool    noise_init;
} fx_state_t;

extern fx_state_t s_st;
extern uint8_t    s_mode;

static inline float mapf(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static inline float constrainf(float x, float min_val, float max_val) {
    return x < min_val ? min_val : (x > max_val ? max_val : x);
}

void     noise_setup(void);
float    noise2d(float x, float y);
uint16_t noise16(uint32_t x, uint32_t y);
void     fade_out(uint8_t rate);
void     draw_bar(int band, int height, rgb_t c, const settings_t* s);
uint8_t  freq_to_color(float freq);
int      freq_to_pos(float freq, int max_pos);

/* 特效函数声明 */
void fx_spectrum(const mic_data_t* d, const settings_t* s);
void fx_freqwave(const mic_data_t* d, const settings_t* s);
void fx_freqmatrix(const mic_data_t* d, const settings_t* s);
void fx_2dgeq(const mic_data_t* d, const settings_t* s);
void fx_waterfall(const mic_data_t* d, const settings_t* s);
void fx_freqpixels(const mic_data_t* d, const settings_t* s);
void fx_binmap(const mic_data_t* d, const settings_t* s);
void fx_freqmap(const mic_data_t* d, const settings_t* s);

void fx_juggles(const mic_data_t* d, const settings_t* s);
void fx_gravimeter(const mic_data_t* d, const settings_t* s);
void fx_gravcenter(const mic_data_t* d, const settings_t* s);
void fx_gravcentric(const mic_data_t* d, const settings_t* s);
void fx_gravfreq(const mic_data_t* d, const settings_t* s);
void fx_2dfunkyplank(const mic_data_t* d, const settings_t* s);

void fx_plasmoid(const mic_data_t* d, const settings_t* s);
void fx_aurora(const mic_data_t* d, const settings_t* s);
void fx_midnoise(const mic_data_t* d, const settings_t* s);
void fx_noisemeter(const mic_data_t* d, const settings_t* s);
void fx_noisefire(const mic_data_t* d, const settings_t* s);
void fx_noisemove(const mic_data_t* d, const settings_t* s);

void fx_pixels(const mic_data_t* d, const settings_t* s);
void fx_pixelwave(const mic_data_t* d, const settings_t* s);
void fx_matripix(const mic_data_t* d, const settings_t* s);
void fx_puddles(const mic_data_t* d, const settings_t* s);
void fx_puddlepeak(const mic_data_t* d, const settings_t* s);
void fx_ripplepeak(const mic_data_t* d, const settings_t* s);
void fx_djlight(const mic_data_t* d, const settings_t* s);
void fx_2dcenterbars(const mic_data_t* d, const settings_t* s);
void fx_blurz(const mic_data_t* d, const settings_t* s);