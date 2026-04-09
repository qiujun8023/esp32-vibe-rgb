#pragma once

// 系统默认配置项
// 修改此文件仅影响首次烧录或恢复出厂设置后的初始状态

// LED 矩阵配置
#define DEF_LED_GPIO       16
#define DEF_LED_W          8
#define DEF_LED_H          8
#define DEF_LED_SERPENTINE 0
#define DEF_LED_START      0
#define DEF_LED_ROTATION   0
#define DEF_BRIGHTNESS     160

// 麦克风配置
#define DEF_MIC_WS     4
#define DEF_MIC_SCK    5
#define DEF_MIC_DIN    6
#define DEF_AGC_MODE   1
#define DEF_GAIN       15.0f
#define DEF_SQUELCH    10
#define DEF_FFT_SMOOTH 100

// 默认特效参数
#define DEF_EFFECT    0
#define DEF_PALETTE   0
#define DEF_SPEED     128
#define DEF_INTENSITY 128
#define DEF_CUSTOM1   128
#define DEF_CUSTOM2   128
#define DEF_CUSTOM3   128
#define DEF_FREQ_DIR  0

// 网络配置
#define WIFI_AP_SSID        "ESP32-Vibe-RGB"
#define WIFI_AP_IP          "10.10.10.10"
#define WIFI_STA_TIMEOUT_MS 15000
#define DEVICE_NAME         "esp32-vibe-rgb"

// 存储配置
#define NVS_NAMESPACE    "led_cfg"
#define NVS_KEY_SETTINGS "settings"