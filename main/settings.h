/**
 * @file settings.h
 * @brief 全局配置结构体与接口
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 系统全局配置
 */
typedef struct {
    /* WiFi 配置 */
    char     ssid[64];
    char     pass[64];
    uint8_t  ip_mode;
    uint32_t s_ip;
    uint32_t s_mask;
    uint32_t s_gw;
    uint32_t s_dns1;
    uint32_t s_dns2;

    /* LED 配置 */
    uint8_t led_gpio;
    uint8_t led_w;
    uint8_t led_h;
    uint8_t led_serpentine;
    uint8_t led_start;
    uint8_t led_rotation;
    uint8_t brightness;

    /* 麦克风配置 */
    uint8_t mic_sck;
    uint8_t mic_ws;
    uint8_t mic_din;
    uint8_t agc_mode;
    float   gain;
    uint8_t squelch;
    uint8_t fft_smooth;

    /* 特效配置 */
    uint8_t effect;
    uint8_t palette;
    uint8_t speed;
    uint8_t intensity;
    uint8_t custom1;
    uint8_t custom2;
    uint8_t custom3;

    uint8_t cfg_version;
} settings_t;

#define SETTINGS_VERSION 1

/**
 * @brief 初始化配置（从 NVS 加载或使用默认值）
 */
void settings_init(void);

/**
 * @brief 获取配置指针（仅限已持锁场景使用）
 */
settings_t* settings_get(void);

/**
 * @brief 加锁拷贝配置快照（推荐使用）
 */
void settings_copy(settings_t* out);

/**
 * @brief 加锁
 */
void settings_lock(void);

/**
 * @brief 解锁
 */
void settings_unlock(void);

/**
 * @brief 保存配置到 NVS
 */
void settings_save(void);

/**
 * @brief 恢复出厂设置并重启
 */
void settings_factory_reset(void);

/**
 * @brief 检查 WiFi 是否已配置
 */
bool settings_wifi_configured(void);