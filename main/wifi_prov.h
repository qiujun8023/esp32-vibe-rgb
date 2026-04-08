#pragma once
// 启动 AP 配网模式（含 WiFi 扫描 + captive portal）
// 阻塞，配网完成后 esp_restart()，不返回
void wifi_prov_start_ap(void);
