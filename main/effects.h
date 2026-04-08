#pragma once
#include <stdint.h>

#include "mic.h"
#include "settings.h"

#define EFFECT_COUNT 22

typedef struct {
    const char* name;
    const char* c1_label;  // "" = 不显示
    const char* c2_label;
    const char* c3_label;
} effect_info_t;

extern const effect_info_t EFFECT_INFO[EFFECT_COUNT];

void effects_init(void);
void effects_update(const mic_data_t* data, const settings_t* s);
// 切换效果时重置内部状态
void effects_set_mode(uint8_t id);
// 暂停/恢复效果更新（用于 LED 测试等）
void effects_pause(void);
void effects_resume(void);
