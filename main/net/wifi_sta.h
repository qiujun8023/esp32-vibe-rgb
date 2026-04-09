#pragma once

// net/wifi_sta.h
// WiFi STA 连接管理：连接、重连、等待结果、RSSI 查询

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

// 初始化并启动 STA 连接（非阻塞）
void wifi_sta_init(const settings_t* s);

// 阻塞等待连接结果，返回 true=已连接，false=超时/失败
bool wifi_sta_wait_connected(uint32_t timeout_ms);

// 获取当前 RSSI（未连接时返回 -100）
int wifi_sta_rssi(void);
