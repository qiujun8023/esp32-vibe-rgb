#pragma once

#include <stdint.h>

#include "settings.h"

#define MIC_BANDS    8
#define MIC_FFT_SIZE 512

typedef struct {
    float bands[MIC_BANDS];
    float volume;
    float peak;
    float beat;
    float dominant_freq;
    float major_peak;
    float major_mag;
} mic_data_t;

void mic_init(const settings_t* s);

/* 加锁快照,调用端拿到的是定帧数据 */
void mic_get_data(mic_data_t* out);

void mic_apply_settings(const settings_t* s);
