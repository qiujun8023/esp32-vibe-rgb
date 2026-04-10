/**
 * @file wifi_prov.h
 * @brief WiFi AP 配网模式接口
 */

#pragma once

/**
 * @brief 启动配网 AP 模式
 *
 * 创建 Captive Portal，等待配网完成后重启
 */
void wifi_prov_start_ap(void);