#pragma once

#include <stdint.h>

#include "settings.h"

#define MIC_BANDS    8
#define MIC_FFT_SIZE 512

/**
 * 麦克风音频分析结果
 */
typedef struct {
    float bands[MIC_BANDS];
    float volume;
    float peak;
    float beat;
    float dominant_freq;
} mic_data_t;

/**
 * 初始化麦克风分析模块
 */
void mic_init(const settings_t* s);

/**
 * 获取最新的音频分析数据（线程安全）
 */
void mic_get_data(mic_data_t* out);

/**
 * 运行时更新音频处理参数
 */
void mic_apply_settings(const settings_t* s);