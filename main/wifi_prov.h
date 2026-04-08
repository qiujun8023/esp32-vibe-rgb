#pragma once

/**
 * WiFi AP 配网模式
 * 启动开放接入点和 Web 服务器，允许用户配置 WiFi 凭据
 * 包含 Captive Portal 功能
 */

/**
 * 启动 AP 配网任务（阻塞直到配网完成）
 */
void wifi_prov_start_ap(void);