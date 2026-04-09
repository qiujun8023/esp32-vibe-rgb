#pragma once

// net/ws_push.h
// WebSocket LED 帧推流任务

#include <esp_http_server.h>

// 启动推流任务（20fps），向所有已连接 WS 客户端广播 LED 帧和音频数据
void ws_push_start(httpd_handle_t server);
