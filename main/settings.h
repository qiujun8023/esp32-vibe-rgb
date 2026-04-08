#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    // ── WiFi ──────────────────────────────
    char     ssid[64];
    char     pass[64];
    uint8_t  ip_mode;  // 0=DHCP  1=静态IP
    uint32_t s_ip;     // 静态IP  (大端序)
    uint32_t s_mask;
    uint32_t s_gw;
    uint32_t s_dns1;
    uint32_t s_dns2;

    // ── LED ───────────────────────────────
    uint8_t led_gpio;
    uint8_t led_w;  // 矩阵宽
    uint8_t led_h;  // 矩阵高
    uint8_t led_serpentine;
    uint8_t led_start;  // 0=左下 1=右下 2=左上 3=右上
    uint8_t brightness;

    // ── 麦克风 ────────────────────────────
    uint8_t mic_sck;
    uint8_t mic_ws;
    uint8_t mic_din;
    uint8_t agc_mode;    // 0=off  1=normal  2=high
    float   gain;        // 手动增益 (agc_mode=0 时生效)
    uint8_t squelch;     // 噪声门限 0-255
    uint8_t fft_smooth;  // FFT 平滑 0-255

    // ── 效果 ──────────────────────────────
    uint8_t effect;
    uint8_t palette;
    uint8_t speed;      // 0-255
    uint8_t intensity;  // 0-255
    uint8_t custom1;
    uint8_t custom2;
    uint8_t custom3;
    uint8_t freq_dir;  // 0=频率左右  1=频率上下
} settings_t;

void        settings_init(void);
settings_t* settings_get(void);
void        settings_save(void);
void        settings_reset_wifi(void);
void        settings_factory_reset(void);  // 清 NVS 后重启
bool        settings_wifi_configured(void);
