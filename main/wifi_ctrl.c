#include "wifi_ctrl.h"

#include <cJSON.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "effects.h"
#include "led.h"
#include "mic.h"
#include "settings.h"

static const char* TAG = "wifi_ctrl";

extern const char     html_ctrl_html_start[] asm("_binary_ctrl_html_start");
extern const char     html_ctrl_js_start[] asm("_binary_ctrl_js_start");
extern const char     html_style_css_start[] asm("_binary_style_css_start");
extern const unsigned ctrl_html_length asm("ctrl_html_length");
extern const unsigned ctrl_js_length asm("ctrl_js_length");
extern const unsigned style_css_length asm("style_css_length");

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_evt;
static httpd_handle_t     s_server;
static int                s_ws_fd = -1;

/**
 * WebSocket 推送任务
 */
static void push_task(void* arg) {
    TickType_t last    = xTaskGetTickCount();
    int        fps_cnt = 0, fps_val = 0;
    TickType_t fps_ts = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(50));
        if (s_ws_fd < 0) continue;

        mic_data_t d;
        mic_get_data(&d);

        fps_cnt++;
        if (xTaskGetTickCount() - fps_ts >= pdMS_TO_TICKS(1000)) {
            fps_val = fps_cnt;
            fps_cnt = 0;
            fps_ts  = xTaskGetTickCount();
        }

        static uint8_t fb[LED_MAX_COUNT * 3];
        int            fblen = 0;
        led_get_fb(fb, &fblen);

        static char msg[LED_MAX_COUNT * 6 + 512];
        int         pos = 0;
        pos += snprintf(msg + pos, sizeof(msg) - pos, "{\"pixels\":\"");
        for (int i = 0; i < fblen; i++) {
            pos += snprintf(msg + pos, sizeof(msg) - pos, "%02x", fb[i]);
        }
        pos += snprintf(msg + pos, sizeof(msg) - pos, "\",\"fps\":%d,\"volume\":%.2f,\"beat\":%.2f,\"bands\":[",
                        fps_val, d.volume, d.beat);
        for (int b = 0; b < MIC_BANDS; b++) {
            pos += snprintf(msg + pos, sizeof(msg) - pos, "%.2f%s", d.bands[b], (b < MIC_BANDS - 1) ? "," : "");
        }
        pos += snprintf(msg + pos, sizeof(msg) - pos, "]}");

        httpd_ws_frame_t pkt = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t*)msg,
            .len     = (size_t)pos,
        };
        httpd_ws_send_frame_async(s_server, s_ws_fd, &pkt);
    }
}

/**
 * 将配置导出为 JSON
 */
static char* settings_to_json(void) {
    settings_lock();
    settings_t* s    = settings_get();
    cJSON*      root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "ssid", s->ssid);
    cJSON_AddNumberToObject(root, "ip_mode", s->ip_mode);

    struct in_addr a;
    char           ip_s[20];
    a.s_addr = s->s_ip;
    cJSON_AddStringToObject(root, "s_ip", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_mask;
    cJSON_AddStringToObject(root, "s_mask", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_gw;
    cJSON_AddStringToObject(root, "s_gw", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_dns1;
    cJSON_AddStringToObject(root, "s_dns1", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_dns2;
    cJSON_AddStringToObject(root, "s_dns2", inet_ntoa_r(a, ip_s, sizeof(ip_s)));

    cJSON_AddNumberToObject(root, "led_gpio", s->led_gpio);
    cJSON_AddNumberToObject(root, "led_w", s->led_w);
    cJSON_AddNumberToObject(root, "led_h", s->led_h);
    cJSON_AddNumberToObject(root, "led_serpentine", s->led_serpentine);
    cJSON_AddNumberToObject(root, "led_start", s->led_start);
    cJSON_AddNumberToObject(root, "led_rotation", s->led_rotation);
    cJSON_AddNumberToObject(root, "brightness", s->brightness);

    cJSON_AddNumberToObject(root, "mic_sck", s->mic_sck);
    cJSON_AddNumberToObject(root, "mic_ws", s->mic_ws);
    cJSON_AddNumberToObject(root, "mic_din", s->mic_din);
    cJSON_AddNumberToObject(root, "agc_mode", s->agc_mode);
    cJSON_AddNumberToObject(root, "gain", s->gain);
    cJSON_AddNumberToObject(root, "squelch", s->squelch);
    cJSON_AddNumberToObject(root, "fft_smooth", s->fft_smooth);

    cJSON_AddNumberToObject(root, "effect", s->effect);
    cJSON_AddNumberToObject(root, "palette", s->palette);
    cJSON_AddNumberToObject(root, "speed", s->speed);
    cJSON_AddNumberToObject(root, "intensity", s->intensity);
    cJSON_AddNumberToObject(root, "custom1", s->custom1);
    cJSON_AddNumberToObject(root, "custom2", s->custom2);
    cJSON_AddNumberToObject(root, "custom3", s->custom3);
    cJSON_AddNumberToObject(root, "freq_dir", s->freq_dir);

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    settings_unlock();
    return str;
}

/**
 * 从 JSON 解析配置
 */
static bool settings_from_json(const char* body, bool* need_restart) {
    cJSON* root = cJSON_Parse(body);
    if (!root) return false;

    settings_lock();
    settings_t* s = settings_get();
    *need_restart = false;

#define GET_INT(key, field)                                                      \
    {                                                                            \
        cJSON* it = cJSON_GetObjectItem(root, key);                              \
        if (it && cJSON_IsNumber(it)) s->field = (typeof(s->field))it->valueint; \
    }
#define GET_FLT(key, field)                                              \
    {                                                                    \
        cJSON* it = cJSON_GetObjectItem(root, key);                      \
        if (it && cJSON_IsNumber(it)) s->field = (float)it->valuedouble; \
    }
#define WATCH_INT(key, field)                                                     \
    {                                                                             \
        cJSON* it = cJSON_GetObjectItem(root, key);                               \
        if (it && cJSON_IsNumber(it)) {                                           \
            if (s->field != (typeof(s->field))it->valueint) *need_restart = true; \
            s->field = (typeof(s->field))it->valueint;                            \
        }                                                                         \
    }

    cJSON* ssid_it = cJSON_GetObjectItem(root, "ssid");
    if (ssid_it && cJSON_IsString(ssid_it) && ssid_it->valuestring[0]) {
        if (strcmp(s->ssid, ssid_it->valuestring)) *need_restart = true;
        strlcpy(s->ssid, ssid_it->valuestring, sizeof(s->ssid));
    }
    cJSON* pass_it = cJSON_GetObjectItem(root, "pass_new");
    if (pass_it && cJSON_IsString(pass_it) && pass_it->valuestring[0]) {
        if (strcmp(s->pass, pass_it->valuestring)) *need_restart = true;
        strlcpy(s->pass, pass_it->valuestring, sizeof(s->pass));
    }

    WATCH_INT("led_gpio", led_gpio);
    WATCH_INT("led_w", led_w);
    WATCH_INT("led_h", led_h);
    WATCH_INT("led_serpentine", led_serpentine);
    WATCH_INT("led_start", led_start);
    GET_INT("led_rotation", led_rotation);
    WATCH_INT("mic_sck", mic_sck);
    WATCH_INT("mic_ws", mic_ws);
    WATCH_INT("mic_din", mic_din);
    GET_INT("ip_mode", ip_mode);

    cJSON* it;
    if ((it = cJSON_GetObjectItem(root, "s_ip")) && cJSON_IsString(it)) s->s_ip = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_mask")) && cJSON_IsString(it)) s->s_mask = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_gw")) && cJSON_IsString(it)) s->s_gw = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_dns1")) && cJSON_IsString(it)) s->s_dns1 = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_dns2")) && cJSON_IsString(it)) s->s_dns2 = inet_addr(it->valuestring);

    GET_INT("brightness", brightness);
    GET_INT("agc_mode", agc_mode);
    GET_FLT("gain", gain);
    GET_INT("squelch", squelch);
    GET_INT("fft_smooth", fft_smooth);
    GET_INT("effect", effect);
    GET_INT("palette", palette);
    GET_INT("speed", speed);
    GET_INT("intensity", intensity);
    GET_INT("custom1", custom1);
    GET_INT("custom2", custom2);
    GET_INT("custom3", custom3);
    GET_INT("freq_dir", freq_dir);

    settings_unlock();
    cJSON_Delete(root);
    return true;
}

static esp_err_t handle_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html_ctrl_html_start, ctrl_html_length);
    return ESP_OK;
}

static esp_err_t handle_css(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, html_style_css_start, style_css_length);
    return ESP_OK;
}

static esp_err_t handle_js(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_send(req, html_ctrl_js_start, ctrl_js_length);
    return ESP_OK;
}

/**
 * WebSocket 处理器
 */
static esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        s_ws_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "websocket connected, fd: %d", s_ws_fd);

        char*            json = settings_to_json();
        httpd_ws_frame_t pkt  = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t*)json, .len = strlen(json)};
        httpd_ws_send_frame(req, &pkt);
        free(json);
        return ESP_OK;
    }

    uint8_t          buf[1024] = {0};
    httpd_ws_frame_t pkt       = {.payload = buf, .type = HTTPD_WS_TYPE_TEXT};
    if (httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1) != ESP_OK) {
        s_ws_fd = -1;
        return ESP_FAIL;
    }
    buf[pkt.len] = '\0';

    cJSON* root = cJSON_Parse((char*)buf);
    if (!root) return ESP_FAIL;

    if (cJSON_GetObjectItem(root, "test_led")) {
        effects_pause();
        int count = led_count();
        for (int i = 0; i < count; i++) {
            led_clear();
            led_set_pixel_idx(i, 255, 0, 0);
            led_flush();
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        led_clear();
        led_flush();
        effects_resume();
        cJSON_Delete(root);
        return ESP_OK;
    }
    cJSON_Delete(root);

    bool restart = false;
    if (settings_from_json((char*)buf, &restart)) {
        settings_t* s = settings_get();
        led_apply_settings(s);
        mic_apply_settings(s);
        effects_set_mode(s->effect);
        settings_save();
        if (restart) {
            ESP_LOGW(TAG, "critical settings changed, restarting");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    }
    return ESP_OK;
}

static esp_err_t handle_save(httpd_req_t* req) {
    char buf[1024] = {0};
    int  len       = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty request body");
        return ESP_FAIL;
    }

    bool restart = false;
    if (!settings_from_json(buf, &restart)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json format");
        return ESP_FAIL;
    }

    settings_save();
    settings_t* s = settings_get();
    led_apply_settings(s);
    mic_apply_settings(s);
    effects_set_mode(s->effect);

    cJSON* r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddBoolToObject(r, "restart_required", restart);
    char* json = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    if (restart) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    return ESP_OK;
}

static esp_err_t handle_reboot(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t handle_reset_wifi(httpd_req_t* req) {
    settings_reset_wifi();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t handle_factory_reset(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    settings_factory_reset();
    return ESP_OK;
}

static int  s_retry = 0;
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

void wifi_ctrl_init(void) {
    s_wifi_evt    = xEventGroupCreate();
    settings_t* s = settings_get();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* sta_if = esp_netif_create_default_wifi_sta();

    esp_netif_set_hostname(sta_if, DEVICE_NAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL, NULL));

    wifi_config_t sta_cfg = {0};
    strlcpy((char*)sta_cfg.sta.ssid, s->ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char*)sta_cfg.sta.password, s->pass, sizeof(sta_cfg.sta.password));

    if (s->ip_mode == 1 && s->s_ip) {
        esp_netif_dhcpc_stop(sta_if);
        esp_netif_ip_info_t info = {.ip.addr = s->s_ip, .netmask.addr = s->s_mask, .gw.addr = s->s_gw};
        esp_netif_set_ip_info(sta_if, &info);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

    ESP_LOGI(TAG, "connecting to wifi: %s", s->ssid);
    EventBits_t bits =
        xEventGroupWaitBits(s_wifi_evt, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, false, false, pdMS_TO_TICKS(15000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "wifi connection failed, clearing credentials");
        settings_reset_wifi();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    httpd_config_t hcfg   = HTTPD_DEFAULT_CONFIG();
    hcfg.max_uri_handlers = 16;
    hcfg.uri_match_fn     = httpd_uri_match_wildcard;
    if (httpd_start(&s_server, &hcfg) == ESP_OK) {
#define REG(u, m, h, w)                                      \
    {                                                        \
        httpd_uri_t _u = {.uri                      = u,     \
                          .method                   = m,     \
                          .handler                  = h,     \
                          .user_ctx                 = NULL,  \
                          .is_websocket             = w,     \
                          .handle_ws_control_frames = false, \
                          .supported_subprotocol    = NULL};    \
        httpd_register_uri_handler(s_server, &_u);           \
    }
        REG("/", HTTP_GET, handle_root, false);
        REG("/style.css", HTTP_GET, handle_css, false);
        REG("/ctrl.js", HTTP_GET, handle_js, false);
        REG("/ws", HTTP_GET, handle_ws, true);
        REG("/api/settings", HTTP_POST, handle_save, false);
        REG("/api/reboot", HTTP_POST, handle_reboot, false);
        REG("/api/reset_wifi", HTTP_POST, handle_reset_wifi, false);
        REG("/api/factory", HTTP_POST, handle_factory_reset, false);
    }

    xTaskCreate(push_task, "ws_push", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "http server started");
}