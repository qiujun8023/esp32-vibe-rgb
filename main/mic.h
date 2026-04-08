#pragma once
#include <stdint.h>

#include "settings.h"

#define MIC_BANDS    8
#define MIC_FFT_SIZE 512

typedef struct {
    float bands[MIC_BANDS];  // 各频段幅度 0.0-1.0
    float volume;            // 整体音量 0.0-1.0
    float peak;              // 慢衰减峰值
    float beat;              // 节拍强度 0.0-1.0
    float dominant_freq;     // 主频段索引 0-7
} mic_data_t;

void mic_init(const settings_t* s);
void mic_get_data(mic_data_t* out);
void mic_apply_settings(const settings_t* s);  // 运行时更新 gain/squelch/agc
