/**
 * @file net_init.c
 * @brief 网络栈初始化：TCP/IP 栈、事件循环
 */

#include "net_init.h"

#include <esp_event.h>
#include <esp_netif.h>

void net_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}