#pragma once

// 所有默认值 —— 网页可覆盖，持久化到 NVS
// 修改这里只影响首次烧录时的初始值

#define DEF_LED_GPIO       16
#define DEF_LED_W          8
#define DEF_LED_H          8
#define DEF_LED_SERPENTINE 1  // 1=蛇形走线
#define DEF_LED_START      0  // 0=左下 1=右下 2=左上 3=右上
#define DEF_BRIGHTNESS     160

#define DEF_MIC_WS     4
#define DEF_MIC_SCK    5
#define DEF_MIC_DIN    6
#define DEF_AGC_MODE   1  // 0=关 1=普通 2=强
#define DEF_GAIN       15.0f
#define DEF_SQUELCH    10
#define DEF_FFT_SMOOTH 100

#define DEF_EFFECT    0  // Spectrum
#define DEF_PALETTE   0  // Rainbow
#define DEF_SPEED     128
#define DEF_INTENSITY 128
#define DEF_CUSTOM1   128
#define DEF_CUSTOM2   128
#define DEF_CUSTOM3   128
#define DEF_FREQ_DIR  0  // 0=左右 1=上下

#define WIFI_AP_SSID        "ESP32-LED"
#define WIFI_AP_IP          "10.10.10.10"
#define WIFI_STA_TIMEOUT_MS 15000

#define NVS_NAMESPACE    "led_cfg"
#define NVS_KEY_SETTINGS "settings"
