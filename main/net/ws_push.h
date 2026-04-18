#pragma once

#include <esp_http_server.h>

/* 启动推流任务,向所有 ws 客户端广播像素与音频数据,约 20 fps */
void ws_push_start(httpd_handle_t server);
