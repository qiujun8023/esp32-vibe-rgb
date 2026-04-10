/**
 * @file config.h
 * @brief 系统默认配置
 *
 * 修改此文件仅影响首次烧录或恢复出厂设置后的初始状态
 */

#pragma once

/* 效果数量 */
#define EFFECT_COUNT 28

/* LED 矩阵配置 */
#define DEF_LED_GPIO       16
#define DEF_LED_W          8
#define DEF_LED_H          8
#define DEF_LED_SERPENTINE 0
#define DEF_LED_START      0
#define DEF_LED_ROTATION   0
#define DEF_BRIGHTNESS     160

/* 麦克风配置 */
#define DEF_MIC_WS     4
#define DEF_MIC_SCK    5
#define DEF_MIC_DIN    6
#define DEF_AGC_MODE   1
#define DEF_GAIN       15.0f
#define DEF_SQUELCH    10
#define DEF_FFT_SMOOTH 100

/* 默认特效参数 */
#define DEF_EFFECT    0
#define DEF_PALETTE   0
#define DEF_SPEED     128
#define DEF_INTENSITY 128
#define DEF_CUSTOM1   128
#define DEF_CUSTOM2   128
#define DEF_CUSTOM3   128

/* 每个效果的默认参数 [效果ID][参数1/2/3] */
#define DEFAULT_EFFECT_PARAMS               \
    {                                       \
        {128, 128, 128}, /* 0: 频谱柱 */    \
        {128, 128, 128}, /* 1: 频谱均衡 */  \
        {128, 128, 128}, /* 2: 中心柱 */    \
        {128, 128, 128}, /* 3: 频谱映射 */  \
        {128, 128, 128}, /* 4: 瀑布流 */    \
        {128, 128, 128}, /* 5: 重力计 */    \
        {128, 128, 128}, /* 6: 重力中心 */  \
        {128, 128, 128}, /* 7: 重力偏心 */  \
        {128, 128, 128}, /* 8: 重力频率 */  \
        {128, 128, 128}, /* 9: 下落木板 */  \
        {128, 128, 128}, /* 10: 矩阵像素 */ \
        {128, 128, 128}, /* 11: 频率波 */   \
        {128, 128, 128}, /* 12: 像素波 */   \
        {128, 128, 128}, /* 13: 涟漪峰值 */ \
        {128, 128, 128}, /* 14: 弹跳球 */   \
        {128, 128, 128}, /* 15: 水塘峰值 */ \
        {128, 128, 128}, /* 16: 水塘 */     \
        {128, 128, 128}, /* 17: 频率像素 */ \
        {128, 128, 128}, /* 18: 频率映射 */ \
        {128, 128, 128}, /* 19: 随机像素 */ \
        {128, 128, 128}, /* 20: 噪声火焰 */ \
        {128, 128, 128}, /* 21: 等离子 */   \
        {128, 128, 128}, /* 22: 极光 */     \
        {128, 128, 128}, /* 23: 中间噪声 */ \
        {128, 128, 128}, /* 24: 噪声计 */   \
        {128, 128, 128}, /* 25: 噪声移动 */ \
        {128, 128, 128}, /* 26: 模糊色块 */ \
        {128, 128, 128}, /* 27: DJ灯光 */   \
    }

/* 网络配置 */
#define WIFI_AP_SSID        "ESP32-Vibe-RGB"
#define WIFI_AP_IP          "10.10.10.10"
#define WIFI_STA_TIMEOUT_MS 15000
#define DEVICE_NAME         "esp32-vibe-rgb"

/* 存储配置 */
#define NVS_NAMESPACE    "led_cfg"
#define NVS_KEY_SETTINGS "settings"