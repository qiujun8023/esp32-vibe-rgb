#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

typedef struct {
    char     ssid[64];
    char     pass[64];
    uint8_t  ip_mode;
    uint32_t s_ip;
    uint32_t s_mask;
    uint32_t s_gw;
    uint32_t s_dns1;
    uint32_t s_dns2;

    uint8_t led_gpio;
    uint8_t led_w;
    uint8_t led_h;
    uint8_t led_serpentine;
    uint8_t led_start;
    uint8_t led_rotation;
    uint8_t brightness;

    uint8_t mic_sck;
    uint8_t mic_ws;
    uint8_t mic_din;
    uint8_t agc_mode;
    float   gain;
    uint8_t squelch;
    uint8_t fft_smooth;

    uint8_t effect;
    uint8_t palette;
    uint8_t speed;
    uint8_t intensity;
    uint8_t custom1;
    uint8_t custom2;
    uint8_t custom3;

    /* 每个效果独立保存 custom1/2/3,切换效果时自动加载,避免用户调好的参数被覆盖 */
    uint8_t effect_params[EFFECT_COUNT][3];

    uint8_t cfg_version;
} settings_t;

#define SETTINGS_VERSION 2

void settings_init(void);

/* 不加锁,仅用于已持锁或只读启动阶段 */
settings_t* settings_get(void);

/* 推荐在任务间访问时使用,内部加锁拷贝一次 */
void settings_copy(settings_t* out);

void settings_lock(void);
void settings_unlock(void);

/* 与上次保存内容比对,无变化时跳过写入以减少 flash 磨损 */
void settings_save(void);

/* 擦除 NVS 并 esp_restart */
void settings_factory_reset(void);

bool settings_wifi_configured(void);

/* 切换效果后调用,把该效果槽位的 custom1/2/3 加载回全局 settings */
void settings_effect_load_params(uint8_t effect_id);
