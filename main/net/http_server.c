#include "http_server.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "effects.h"
#include "led.h"
#include "mic.h"
#include "net/settings_json.h"
#include "net/ws_push.h"
#include "settings.h"

static const char* TAG = "http_server";

extern const char     html_ctrl_html_start[] asm("_binary_ctrl_html_start");
extern const char     html_ctrl_js_start[] asm("_binary_ctrl_js_start");
extern const char     html_style_css_start[] asm("_binary_style_css_start");
extern const unsigned ctrl_html_length asm("ctrl_html_length");
extern const unsigned ctrl_js_length asm("ctrl_js_length");
extern const unsigned style_css_length asm("style_css_length");

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

static esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        ESP_LOGD(TAG, "ws connected, fd: %d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    uint8_t          buf[1024] = {0};
    httpd_ws_frame_t pkt       = {.payload = buf, .type = HTTPD_WS_TYPE_TEXT};
    esp_err_t        ret       = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);

    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_NOT_FOUND) return ret;
        return ESP_OK;
    }

    if (pkt.type == HTTPD_WS_TYPE_PONG) return ESP_OK;

    if (pkt.type == HTTPD_WS_TYPE_PING) {
        httpd_ws_frame_t pong = {.type = HTTPD_WS_TYPE_PONG, .payload = buf, .len = pkt.len};
        return httpd_ws_send_frame(req, &pong);
    }

    if (pkt.type == HTTPD_WS_TYPE_CLOSE || pkt.type == HTTPD_WS_TYPE_BINARY) return ESP_OK;

    buf[pkt.len] = '\0';

    /* 文本层 ping/pong,浏览器 WebSocket API 无法发 control frame 时使用 */
    if (pkt.len == 4 && strcmp((char*)buf, "ping") == 0) {
        httpd_ws_frame_t pong = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t*)"pong", .len = 4};
        return httpd_ws_send_frame(req, &pong);
    }

    cJSON* root = cJSON_Parse((char*)buf);
    if (!root) return ESP_OK;

    if (cJSON_GetObjectItem(root, "get_cfg")) {
        settings_t snap;
        settings_copy(&snap);
        char* json = settings_to_json(&snap);
        if (json) {
            httpd_ws_frame_t resp = {
                .type    = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t*)json,
                .len     = strlen(json),
            };
            httpd_ws_send_frame(req, &resp);
            free(json);
        }
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (cJSON_GetObjectItem(root, "test_led")) {
        effects_pause();
        int count = led_count();
        for (int i = 0; i < count; i++) {
            led_hw_test_pixel(i, 255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        led_hw_test_pixel(-1, 0, 0, 0);
        effects_resume();
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (cJSON_GetObjectItem(root, "save")) {
        settings_save();
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (cJSON_GetObjectItem(root, "reboot")) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "reboot requested");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return ESP_OK;
    }

    if (cJSON_GetObjectItem(root, "factory")) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "factory reset requested");
        settings_factory_reset();
        return ESP_OK;
    }

    bool restart = false;
    settings_lock();
    settings_t* s = settings_get();
    settings_from_cjson(root, s, &restart);
    settings_unlock();

    settings_t snap;
    settings_copy(&snap);
    led_apply_settings(&snap);
    mic_apply_settings(&snap);
    effects_set_mode(snap.effect);

    /* 回传完整配置,前端据此同步 custom1/2/3 等 */
    char* json = settings_to_json(&snap);
    if (json) {
        httpd_ws_frame_t resp = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t*)json,
            .len     = strlen(json),
        };
        httpd_ws_send_frame(req, &resp);
        free(json);
    }

    if (restart) {
        settings_save();
        ESP_LOGW(TAG, "critical settings changed, restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_save(httpd_req_t* req) {
    char buf[1024] = {0};
    int  len       = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty request body");
        return ESP_FAIL;
    }

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json format");
        return ESP_FAIL;
    }

    bool restart = false;
    settings_lock();
    settings_t* s = settings_get();
    settings_from_cjson(root, s, &restart);
    settings_unlock();
    cJSON_Delete(root);

    settings_save();

    settings_t snap;
    settings_copy(&snap);
    led_apply_settings(&snap);
    mic_apply_settings(&snap);
    effects_set_mode(snap.effect);

    char* json = settings_to_json(&snap);
    httpd_resp_set_type(req, "application/json");
    if (json) {
        httpd_resp_sendstr(req, json);
        free(json);
    } else {
        httpd_resp_sendstr(req, "{\"ok\":true}");
    }

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

static esp_err_t handle_factory_reset(httpd_req_t* req) {
    ESP_LOGW(TAG, "factory reset requested");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    settings_factory_reset();
    return ESP_OK;
}

static void register_uri(httpd_handle_t srv, const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*),
                         bool ws) {
    httpd_uri_t u = {
        .uri                      = uri,
        .method                   = method,
        .handler                  = handler,
        .user_ctx                 = NULL,
        .is_websocket             = ws,
        .handle_ws_control_frames = ws,
        .supported_subprotocol    = NULL,
    };
    httpd_register_uri_handler(srv, &u);
}

void http_server_start(void) {
    httpd_config_t hcfg   = HTTPD_DEFAULT_CONFIG();
    hcfg.max_uri_handlers = 16;
    hcfg.stack_size       = 8192;
    hcfg.uri_match_fn     = httpd_uri_match_wildcard;
    /* 5s 无读写即关闭连接,防止假死客户端长期占用 socket */
    hcfg.recv_wait_timeout = 5;
    hcfg.send_wait_timeout = 5;
    hcfg.lru_purge_enable  = true;

    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &hcfg) != ESP_OK) {
        ESP_LOGE(TAG, "http server start failed");
        return;
    }

    register_uri(srv, "/", HTTP_GET, handle_root, false);
    register_uri(srv, "/style.css", HTTP_GET, handle_css, false);
    register_uri(srv, "/ctrl.js", HTTP_GET, handle_js, false);
    register_uri(srv, "/ws", HTTP_GET, handle_ws, true);
    register_uri(srv, "/api/settings", HTTP_POST, handle_save, false);
    register_uri(srv, "/api/reboot", HTTP_POST, handle_reboot, false);
    register_uri(srv, "/api/factory", HTTP_POST, handle_factory_reset, false);

    ws_push_start(srv);
    ESP_LOGI(TAG, "http server ready");
}