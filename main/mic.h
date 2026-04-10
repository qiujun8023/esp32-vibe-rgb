/**
 * @file mic.h
 * @brief 麦克风音频采集与分析接口
 */

#pragma once

#include <stdint.h>

#include "settings.h"

#define MIC_BANDS    8
#define MIC_FFT_SIZE 512

/**
 * @brief 音频分析结果
 */
typedef struct {
    float bands[MIC_BANDS]; /* 频带能量 */
    float volume;           /* 音量 */
    float peak;             /* 峰值 */
    float beat;             /* 节拍强度 */
    float dominant_freq;    /* 主频带索引 */
    float major_peak;       /* 主峰频率 */
    float major_mag;        /* 主峰幅度 */
} mic_data_t;

/**
 * @brief 初始化麦克风
 */
void mic_init(const settings_t* s);

/**
 * @brief 获取音频分析数据（加锁拷贝）
 */
void mic_get_data(mic_data_t* out);

/**
 * @brief 应用新设置
 */
void mic_apply_settings(const settings_t* s);