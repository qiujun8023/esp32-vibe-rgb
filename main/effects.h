/**
 * @file effects.h
 * @brief 特效系统接口
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EFFECT_COUNT 28

/* 经典频谱类 */
#define FX_SPECTRUM     0
#define FX_2DGEQ        1
#define FX_2DCENTERBARS 2
#define FX_BINMAP       3
#define FX_WATERFALL    4

/* 重力流动类 */
#define FX_GRAVIMETER   5
#define FX_GRAVCENTER   6
#define FX_GRAVCENTRIC  7
#define FX_GRAVFREQ     8
#define FX_2DFUNKYPLANK 9
#define FX_MATRIPIX     10

/* 粒子波动类 */
#define FX_FREQWAVE   11
#define FX_PIXELWAVE  12
#define FX_RIPPLEPEAK 13
#define FX_JUGGLES    14
#define FX_PUDDLEPEAK 15
#define FX_PUDDLES    16
#define FX_FREQPIXELS 17
#define FX_FREQMAP    18
#define FX_PIXELS     19

/* 噪声抽象类 */
#define FX_NOISEFIRE  20
#define FX_PLASMOID   21
#define FX_AURORA     22
#define FX_MIDNOISE   23
#define FX_NOISEMETER 24
#define FX_NOISEMOVE  25
#define FX_BLURZ      26
#define FX_DJLIGHT    27

/**
 * @brief 特效信息结构
 */
typedef struct {
    const char* name;
    const char* label_c1;
    const char* label_c2;
    const char* label_c3;
} effect_info_t;

/**
 * @brief 初始化特效系统
 */
void effects_init(void);

/**
 * @brief 设置当前特效
 */
void effects_set_mode(uint8_t id);

#include "mic.h"

/**
 * @brief 更新特效帧
 */
void effects_update(const mic_data_t* data, const settings_t* s);

/**
 * @brief 暂停特效
 */
void effects_pause(void);

/**
 * @brief 恢复特效
 */
void effects_resume(void);

/**
 * @brief 检查是否暂停
 */
bool effects_is_paused(void);