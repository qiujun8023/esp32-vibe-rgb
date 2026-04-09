/**
 * @file wifi_prov.c
 * @brief WiFi AP配网模式：Captive Portal + DNS劫持
 */
#include "wifi_prov.h"

#include <cJSON.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "settings.h"

static const char* TAG = "wifi_prov";

#define PROV_DONE_BIT BIT0
static EventGroupHandle_t s_evt;

extern const char     html_prov_html_start[] asm("_binary_prov_html_start");
extern const char     html_style_css_start[] asm("_binary_style_css_start");
extern const unsigned prov_html_length       asm("prov_html_length");
extern const unsigned style_css_length       asm("style_css_length");

/**
 * @brief URL解码
 */
static void url_decode(char* dst, const char* src, size_t maxlen) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < maxlen - 1; i++) {
        if (src[i] == '+') {
            dst[j++] = ' ';
        } else if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            char hex[3] = {src[i + 1], src[i + 2], 0};
            dst[j++]    = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static void extract_field(const char* body, const char* key, char* out, size_t outlen) {
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);
    const char* p = strstr(body, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    const char* end = strchr(p, '&');
    size_t      len = end ? (size_t)(end - p) : strlen(p);

    char   raw[128] = {0};
    size_t copy_len = (len < sizeof(raw) - 1) ? len : sizeof(raw) - 1;
    memcpy(raw, p, copy_len);
    url_decode(out, raw, outlen);
}

/**
 * @brief 扫描WiFi热点
 */
static esp_err_t handle_scan(httpd_req_t* req) {
    esp_wifi_scan_start(NULL, true);
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > 20) ap_num = 20;

    wifi_ap_record_t* aps = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (!aps) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    esp_wifi_scan_get_ap_records(&ap_num, aps);

    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < ap_num; i++) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char*)aps[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(item, "auth", aps[i].authmode);
        cJSON_AddItemToArray(arr, item);
    }
    free(aps);

    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

/**
 * @brief 处理配网请求
 */
static esp_err_t handle_prov(httpd_req_t* req) {
    char body[512] = {0};
    int  len       = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) return ESP_FAIL;

    char ssid[64] = {0}, pass[64] = {0};
    char ip_mode_s[4] = {0}, ip_s[20] = {0}, mask_s[20] = {0}, gw_s[20] = {0};
    char dns1_s[20] = {0}, dns2_s[20] = {0};

    extract_field(body, "ssid",    ssid,      sizeof(ssid));
    extract_field(body, "pass",    pass,      sizeof(pass));
    extract_field(body, "ip_mode", ip_mode_s, sizeof(ip_mode_s));
    extract_field(body, "ip",      ip_s,      sizeof(ip_s));
    extract_field(body, "mask",    mask_s,    sizeof(mask_s));
    extract_field(body, "gw",      gw_s,      sizeof(gw_s));
    extract_field(body, "dns1",    dns1_s,    sizeof(dns1_s));
    extract_field(body, "dns2",    dns2_s,    sizeof(dns2_s));

    if (strlen(ssid) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"ssid_required\"}");
        return ESP_OK;
    }

    settings_lock();
    settings_t* s = settings_get();
    strlcpy(s->ssid, ssid, sizeof(s->ssid));
    strlcpy(s->pass, pass, sizeof(s->pass));
    s->ip_mode = atoi(ip_mode_s);
    if (s->ip_mode == 1) {
        s->s_ip   = inet_addr(ip_s);
        s->s_mask = inet_addr(mask_s);
        s->s_gw   = inet_addr(gw_s);
        s->s_dns1 = inet_addr(dns1_s);
        s->s_dns2 = inet_addr(dns2_s);
    }
    settings_unlock();
    settings_save();

    ESP_LOGI(TAG, "wifi credentials received: %s", ssid);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    xEventGroupSetBits(s_evt, PROV_DONE_BIT);
    return ESP_OK;
}

/**
 * @brief 返回CSS样式
 */
static esp_err_t handle_css(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, html_style_css_start, style_css_length);
    return ESP_OK;
}

/**
 * @brief 返回配网页面
 */
static esp_err_t handle_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html_prov_html_start, prov_html_length);
    return ESP_OK;
}

/**
 * @brief 通用重定向处理
 */
static esp_err_t handle_catch(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief DNS劫持任务（Captive Portal）
 */
static void dns_task(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket creation failed");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns socket bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t            buf[512];
    struct sockaddr_in cli;
    socklen_t          clen = sizeof(cli);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&cli, &clen);
        if (n < 12) continue;

        uint8_t resp[512];
        memcpy(resp, buf, n);
        resp[2] = 0x81; resp[3] = 0x80;
        resp[6] = 0x00; resp[7] = 0x01;

        int pos      = n;
        resp[pos++]  = 0xC0; resp[pos++] = 0x0C;
        resp[pos++]  = 0x00; resp[pos++] = 0x01;
        resp[pos++]  = 0x00; resp[pos++] = 0x01;
        resp[pos++]  = 0x00; resp[pos++] = 0x00;
        resp[pos++]  = 0x00; resp[pos++] = 60;
        resp[pos++]  = 0x00; resp[pos++] = 0x04;
        resp[pos++]  = 10;   resp[pos++] = 10;
        resp[pos++]  = 10;   resp[pos++] = 10;

        sendto(sock, resp, pos, 0, (struct sockaddr*)&cli, clen);
    }
}

/**
 * @brief 启动配网AP模式
 */
void wifi_prov_start_ap(void) {
    s_evt = xEventGroupCreate();

    // 注意：esp_netif_init 和 esp_event_loop_create_default 已由 net_init() 调用
    esp_netif_t* ap_if = esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_cfg = {
        .ap = {.channel = 1, .authmode = WIFI_AUTH_OPEN, .max_connection = 4},
    };
    strlcpy((char*)ap_cfg.ap.ssid, WIFI_AP_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen((char*)ap_cfg.ap.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_if));
    esp_netif_ip_info_t ip = {
        .ip      = {.addr = ESP_IP4TOADDR(10, 10, 10, 10)},
        .gw      = {.addr = ESP_IP4TOADDR(10, 10, 10, 10)},
        .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
    };
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_if, &ip));

    esp_netif_dns_info_t dns = {
        .ip.u_addr.ip4.addr = ESP_IP4TOADDR(10, 10, 10, 10),
        .ip.type            = IPADDR_TYPE_V4,
    };
    ESP_ERROR_CHECK(esp_netif_set_dns_info(ap_if, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_if));

    ESP_LOGI(TAG, "provisioning ap started, ssid: %s, ip: 10.10.10.10", WIFI_AP_SSID);

    httpd_config_t hcfg   = HTTPD_DEFAULT_CONFIG();
    hcfg.lru_purge_enable = true;
    hcfg.uri_match_fn     = httpd_uri_match_wildcard;
    httpd_handle_t srv    = NULL;
    if (httpd_start(&srv, &hcfg) == ESP_OK) {
        httpd_uri_t u_root  = {.uri = "/",          .method = HTTP_GET,  .handler = handle_root};
        httpd_uri_t u_css   = {.uri = "/style.css", .method = HTTP_GET,  .handler = handle_css};
        httpd_uri_t u_scan  = {.uri = "/api/scan",  .method = HTTP_GET,  .handler = handle_scan};
        httpd_uri_t u_prov  = {.uri = "/api/prov",  .method = HTTP_POST, .handler = handle_prov};
        httpd_uri_t u_catch = {.uri = "/*",         .method = HTTP_GET,  .handler = handle_catch};

        httpd_register_uri_handler(srv, &u_root);
        httpd_register_uri_handler(srv, &u_css);
        httpd_register_uri_handler(srv, &u_scan);
        httpd_register_uri_handler(srv, &u_prov);
        httpd_register_uri_handler(srv, &u_catch);
    }

    if (xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "dns task creation failed, captive portal redirect will not work");
    }

    ESP_LOGI(TAG, "waiting for provisioning completion");
    xEventGroupWaitBits(s_evt, PROV_DONE_BIT, false, true, portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "provisioning done, restarting");
    esp_restart();
}
