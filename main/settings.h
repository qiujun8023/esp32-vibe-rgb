#pragma once

#include <stdbool.h>
#include <stdint.h>

// 系统全局配置结构体
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
    uint8_t freq_dir;

    uint8_t cfg_version;
} settings_t;

#define SETTINGS_VERSION 1

void settings_init(void);
settings_t* settings_get(void);         // 仅限已持锁场景内部使用
void settings_copy(settings_t* out);    // 加锁拷贝快照，立即释放，推荐使用
void settings_lock(void);
void settings_unlock(void);
void settings_save(void);
void settings_factory_reset(void);
bool settings_wifi_configured(void);