// net/net_init.c
// 网络栈统一初始化，app_main 调用一次，避免 wifi_prov / wifi_sta 重复初始化

#include "net_init.h"

#include <esp_netif.h>
#include <esp_event.h>

void net_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}
