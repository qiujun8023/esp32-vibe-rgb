/**
 * @file net_init.c
 * @brief 网络栈统一初始化
 */
#include "net_init.h"

#include <esp_netif.h>
#include <esp_event.h>

/**
 * @brief 初始化TCP/IP栈和事件循环
 */
void net_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}
