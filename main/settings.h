#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * 系统全局配置结构体
 */
typedef struct {
    // WiFi 配置
    char     ssid[64];
    char     pass[64];
    uint8_t  ip_mode;
    uint32_t s_ip;
    uint32_t s_mask;
    uint32_t s_gw;
    uint32_t s_dns1;
    uint32_t s_dns2;

    // LED 矩阵配置
    uint8_t led_gpio;
    uint8_t led_w;
    uint8_t led_h;
    uint8_t led_serpentine;
    uint8_t led_start;
    uint8_t led_rotation;
    uint8_t brightness;

    // 麦克风与音频分析配置
    uint8_t mic_sck;
    uint8_t mic_ws;
    uint8_t mic_din;
    uint8_t agc_mode;
    float   gain;
    uint8_t squelch;
    uint8_t fft_smooth;

    // 特效参数
    uint8_t effect;
    uint8_t palette;
    uint8_t speed;
    uint8_t intensity;
    uint8_t custom1;
    uint8_t custom2;
    uint8_t custom3;
    uint8_t freq_dir;
} settings_t;

/**
 * 初始化配置模块，从 NVS 加载配置
 */
void settings_init(void);

/**
 * 获取配置结构体指针（访问前需加锁）
 */
settings_t* settings_get(void);

/**
 * 加锁配置互斥量
 */
void settings_lock(void);

/**
 * 解锁配置互斥量
 */
void settings_unlock(void);

/**
 * 将配置持久化到 NVS
 */
void settings_save(void);

/**
 * 清除 WiFi 配置
 */
void settings_reset_wifi(void);

/**
 * 恢复出厂设置并重启
 */
void settings_factory_reset(void);

/**
 * 检查 WiFi 是否已配置
 */
bool settings_wifi_configured(void);