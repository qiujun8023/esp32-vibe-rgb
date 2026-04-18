#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

/* 非阻塞,后台异步连接 */
void wifi_sta_init(const settings_t* s);

/* true: 已连接；false: 超时或连续失败达阈值（但后台仍会继续重试）*/
bool wifi_sta_wait_connected(uint32_t timeout_ms);

/* 未连接返回 -100 */
int wifi_sta_rssi(void);
