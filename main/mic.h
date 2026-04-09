#pragma once

#include <stdint.h>

#include "settings.h"

#define MIC_BANDS    8
#define MIC_FFT_SIZE 512

// 麦克风音频分析结果
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
void mic_get_data(mic_data_t* out);
void mic_apply_settings(const settings_t* s);