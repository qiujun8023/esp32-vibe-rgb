#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mic.h"
#include "settings.h"

#define EFFECT_COUNT 22

/**
 * 灯效元信息结构体
 */
typedef struct {
    const char* name;
    const char *c1, *c2, *c3;
} effect_info_t;

extern const effect_info_t EFFECT_INFO[EFFECT_COUNT];

/**
 * 初始化灯效引擎
 */
void effects_init(void);

/**
 * 切换当前灯效模式
 */
void effects_set_mode(uint8_t id);

/**
 * 更新并渲染一帧灯效
 */
void effects_update(const mic_data_t* data, const settings_t* s);

/**
 * 暂停灯效更新
 */
void effects_pause(void);

/**
 * 恢复灯效更新
 */
void effects_resume(void);