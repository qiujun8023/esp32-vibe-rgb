// net/wifi_sta.c
// WiFi STA 连接管理：连接、重连（最多5次）、等待结果
// 修复：连接失败时仅清除 SSID/密码，保留 LED/音效配置，不做全量出厂重置

#include "wifi_sta.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <lwip/inet.h>
#include <string.h>

#include "config.h"
#include "settings.h"

static const char* TAG = "wifi_sta";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_evt;
static int                s_retry = 0;

static void wifi_handler(void* arg, esp_event_base_t b, int32_t id, void* data) {
    if (b == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry++ < 5) {
            esp_wifi_connect();
            ESP_LOGW(TAG, "wifi disconnected, retrying %d/5", s_retry);
        } else {
            xEventGroupSetBits(s_wifi_evt, WIFI_FAIL_BIT);
        }
    } else if (b == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "wifi connected, ip: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_evt, WIFI_CONNECTED_BIT);
    }
}

void wifi_sta_init(const settings_t* s) {
    s_wifi_evt = xEventGroupCreate();
    s_retry    = 0;

    esp_netif_t* sta_if = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta_if, DEVICE_NAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL, NULL));

    wifi_config_t sta_cfg = {0};
    strlcpy((char*)sta_cfg.sta.ssid,     s->ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char*)sta_cfg.sta.password, s->pass,  sizeof(sta_cfg.sta.password));

    // 静态 IP 配置
    if (s->ip_mode == 1 && s->s_ip) {
        esp_netif_dhcpc_stop(sta_if);
        esp_netif_ip_info_t info = {
            .ip.addr      = s->s_ip,
            .netmask.addr = s->s_mask,
            .gw.addr      = s->s_gw,
        };
        esp_netif_set_ip_info(sta_if, &info);

        if (s->s_dns1) {
            esp_netif_dns_info_t dns = {
                .ip.u_addr.ip4.addr = s->s_dns1,
                .ip.type            = IPADDR_TYPE_V4,
            };
            esp_netif_set_dns_info(sta_if, ESP_NETIF_DNS_MAIN, &dns);
        }
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

    ESP_LOGI(TAG, "connecting to wifi: %s", s->ssid);
}

bool wifi_sta_wait_connected(uint32_t timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_evt,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        false, false,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

int wifi_sta_rssi(void) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return -100;
}
