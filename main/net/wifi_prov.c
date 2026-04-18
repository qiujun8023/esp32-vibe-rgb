/* 配网 AP：SoftAP + DHCP Option 114 + DNS 劫持 + 客户端白名单 */

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
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/sockets.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "net/captive_dns.h"
#include "settings.h"

static const char* TAG = "wifi_prov";

#define PROV_DONE_BIT BIT0
static EventGroupHandle_t s_evt;

/* DHCP option 114 (RFC 8910): iOS 14+ / Android 11+ 据此自动弹出 captive portal */
static char s_portal_uri[] = "http://192.168.4.1/";

extern const char     html_prov_html_start[] asm("_binary_prov_html_start");
extern const char     html_style_css_start[] asm("_binary_style_css_start");
extern const unsigned prov_html_length asm("prov_html_length");
extern const unsigned style_css_length asm("style_css_length");

/* captive portal 客户端白名单（环形队列）*/
static uint32_t          s_portal_done[CONFIG_ESP_MAX_STA_CONN];
static uint8_t           s_portal_count = 0;
static uint8_t           s_portal_head  = 0;
static SemaphoreHandle_t s_portal_lock  = NULL;

static uint32_t get_client_ip(httpd_req_t* req) {
    int                fd = httpd_req_to_sockfd(req);
    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr*)&addr, &len) != 0)
        return 0;
    return addr.sin_addr.s_addr;
}

static bool portal_is_done_locked(uint32_t ip) {
    for (int i = 0; i < s_portal_count; i++) {
        if (s_portal_done[i] == ip)
            return true;
    }
    return false;
}

static bool portal_is_done(uint32_t ip) {
    if (!s_portal_lock) return false;
    xSemaphoreTake(s_portal_lock, portMAX_DELAY);
    bool r = portal_is_done_locked(ip);
    xSemaphoreGive(s_portal_lock);
    return r;
}

/* 环形覆盖:满后用新 IP 替换最老条目,避免长时间运行后拒绝新客户端 */
static void portal_mark_done(uint32_t ip) {
    if (!ip || !s_portal_lock) return;
    xSemaphoreTake(s_portal_lock, portMAX_DELAY);
    if (!portal_is_done_locked(ip)) {
        s_portal_done[s_portal_head] = ip;
        s_portal_head                = (s_portal_head + 1) % CONFIG_ESP_MAX_STA_CONN;
        if (s_portal_count < CONFIG_ESP_MAX_STA_CONN)
            s_portal_count++;
    }
    xSemaphoreGive(s_portal_lock);
}

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
    if (!p) {
        out[0] = '\0';
        return;
    }
    p += strlen(search);
    const char* end = strchr(p, '&');
    size_t      len = end ? (size_t)(end - p) : strlen(p);

    char   raw[128] = {0};
    size_t copy_len = (len < sizeof(raw) - 1) ? len : sizeof(raw) - 1;
    memcpy(raw, p, copy_len);
    url_decode(out, raw, outlen);
}

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

static esp_err_t handle_prov(httpd_req_t* req) {
    char body[512] = {0};
    int  len       = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) return ESP_FAIL;

    char ssid[64] = {0}, pass[64] = {0};
    char ip_mode_s[4] = {0}, ip_s[20] = {0}, mask_s[20] = {0}, gw_s[20] = {0};
    char dns1_s[20] = {0}, dns2_s[20] = {0};

    extract_field(body, "ssid", ssid, sizeof(ssid));
    extract_field(body, "pass", pass, sizeof(pass));
    extract_field(body, "ip_mode", ip_mode_s, sizeof(ip_mode_s));
    extract_field(body, "ip", ip_s, sizeof(ip_s));
    extract_field(body, "mask", mask_s, sizeof(mask_s));
    extract_field(body, "gw", gw_s, sizeof(gw_s));
    extract_field(body, "dns1", dns1_s, sizeof(dns1_s));
    extract_field(body, "dns2", dns2_s, sizeof(dns2_s));

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

    ESP_LOGI(TAG, "wifi credentials received, ssid: %s", ssid);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    xEventGroupSetBits(s_evt, PROV_DONE_BIT);
    return ESP_OK;
}

static esp_err_t handle_css(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, html_style_css_start, style_css_length);
    return ESP_OK;
}

static esp_err_t handle_root(httpd_req_t* req) {
    /* 客户端打开配网页即视为已完成 captive,后续探测直接放行,不再重复弹窗 */
    portal_mark_done(get_client_ip(req));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html_prov_html_start, prov_html_length);
    return ESP_OK;
}

/* 白名单客户端对各平台连通性探测返回预期响应,避免系统判定无网切 4G */
static esp_err_t reply_connectivity_success(httpd_req_t* req) {
    const char* uri = req->uri;

    /* Android / Chrome */
    if (strstr(uri, "generate_204") || strstr(uri, "gen_204")) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    /* Apple iOS / macOS */
    if (strstr(uri, "hotspot-detect.html")) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_sendstr(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    }
    /* Windows */
    if (strstr(uri, "connecttest.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Microsoft Connect Test");
    }
    if (strstr(uri, "ncsi.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Microsoft NCSI");
    }
    /* Firefox */
    if (strstr(uri, "canonical.html") || strstr(uri, "success.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "success\n");
    }

    /* 白名单但访问了非探测 URL 仍重定向到主页 */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_sendstr(req, "Redirect to captive portal");
}

/* 未注册 URL 统一走这里:新客户端 303 弹 portal,白名单返回探测成功 */
static esp_err_t handle_404(httpd_req_t* req, httpd_err_code_t err) {
    (void)err;
    uint32_t ip = get_client_ip(req);
    if (ip && portal_is_done(ip)) {
        return reply_connectivity_success(req);
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    /* iOS 需要非空 body 才会判定为 captive 弹窗 */
    return httpd_resp_sendstr(req, "Redirect to captive portal");
}

static void configure_dhcps_captive(esp_netif_t* ap_netif) {
    esp_netif_dhcps_stop(ap_netif);

    /* 声明自己为客户端主 DNS,captive_dns 才能截获域名解析 */
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    IP4_ADDR(&dns.ip.u_addr.ip4, 192, 168, 4, 1);
    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);

    uint8_t offer_dns = 1;
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, s_portal_uri,
                           sizeof(s_portal_uri) - 1);

    esp_netif_dhcps_start(ap_netif);
}

void wifi_prov_start_ap(void) {
    if (!s_portal_lock) s_portal_lock = xSemaphoreCreateMutex();
    s_evt = xEventGroupCreate();

    esp_netif_t* ap_if = esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    /* 沿用默认 AP IP 192.168.4.1,只配 DHCP option 114 和 DNS */
    configure_dhcps_captive(ap_if);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_cfg = {
        .ap = {.channel = 1, .authmode = WIFI_AUTH_OPEN, .max_connection = CONFIG_ESP_MAX_STA_CONN},
    };
    strlcpy((char*)ap_cfg.ap.ssid, CONFIG_ESP_WIFI_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen((char*)ap_cfg.ap.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "provisioning ap ready, ssid: %s, ip: 192.168.4.1", CONFIG_ESP_WIFI_SSID);

    httpd_config_t hcfg   = HTTPD_DEFAULT_CONFIG();
    hcfg.lru_purge_enable = true;
    httpd_handle_t srv    = NULL;

    /* captive 期间 404 重定向会刷屏,降日志级别 */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    if (httpd_start(&srv, &hcfg) == ESP_OK) {
        httpd_uri_t u_root = {.uri = "/", .method = HTTP_GET, .handler = handle_root};
        httpd_uri_t u_css  = {.uri = "/style.css", .method = HTTP_GET, .handler = handle_css};
        httpd_uri_t u_scan = {.uri = "/api/scan", .method = HTTP_GET, .handler = handle_scan};
        httpd_uri_t u_prov = {.uri = "/api/prov", .method = HTTP_POST, .handler = handle_prov};

        httpd_register_uri_handler(srv, &u_root);
        httpd_register_uri_handler(srv, &u_css);
        httpd_register_uri_handler(srv, &u_scan);
        httpd_register_uri_handler(srv, &u_prov);
        httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, handle_404);
    }

    captive_dns_start();

    xEventGroupWaitBits(s_evt, PROV_DONE_BIT, false, true, portMAX_DELAY);

    /* 给前端收到响应的时间,再重启 */
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "provisioning done, restarting");
    esp_restart();
}
