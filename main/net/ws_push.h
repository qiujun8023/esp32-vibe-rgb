/**
 * @file ws_push.h
 * @brief WebSocket LED 帧推流接口
 */

#pragma once

#include <esp_http_server.h>

/**
 * @brief 启动推流任务
 *
 * 向所有已连接 WS 客户端广播 LED 帧和音频数据（约 20 fps）
 */
void ws_push_start(httpd_handle_t server);