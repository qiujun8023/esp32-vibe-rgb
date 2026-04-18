#include "net_init.h"

#include <esp_event.h>
#include <esp_netif.h>

void net_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}
