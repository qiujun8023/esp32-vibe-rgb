/**
 * @file wifi_sta.h
 * @brief WiFi STA 连接管理接口
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

/**
 * @brief 初始化并启动 STA 连接（非阻塞）
 */
void wifi_sta_init(const settings_t* s);

/**
 * @brief 阻塞等待连接结果
 *
 * @return true 已连接，false 超时或失败
 */
bool wifi_sta_wait_connected(uint32_t timeout_ms);

/**
 * @brief 获取当前 RSSI（未连接返回 -100）
 */
int wifi_sta_rssi(void);