#include "ws_push.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "led.h"
#include "mic.h"
#include "net/wifi_sta.h"

static const char* TAG = "ws_push";

#define MAX_HTTP_SOCKETS 16
#define MAX_WS_CLIENTS   4

static httpd_handle_t s_server_ref = NULL;

static void push_task(void* arg) {
    TickType_t last           = xTaskGetTickCount();
    TickType_t fps_ts         = xTaskGetTickCount();
    TickType_t last_ping_tick = xTaskGetTickCount();
    int        fps_cnt = 0, fps_val = 0;

    /* 任务常驻，用 static 而非栈或 heap：栈会超，heap 会频繁分配释放 */
    static uint8_t fb[LED_MAX_COUNT * 3];
    static char    msg[LED_MAX_COUNT * 6 + 1024];

    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(50));

        if (!s_server_ref) continue;

        size_t fds_count = MAX_HTTP_SOCKETS;
        int    client_fds[MAX_HTTP_SOCKETS];
        if (httpd_get_client_list(s_server_ref, &fds_count, client_fds) != ESP_OK) continue;

        int ws_fds[MAX_WS_CLIENTS];
        int ws_count = 0;
        for (size_t i = 0; i < fds_count && ws_count < MAX_WS_CLIENTS; i++) {
            if (httpd_ws_get_fd_info(s_server_ref, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                ws_fds[ws_count++] = client_fds[i];
            }
        }
        if (ws_count == 0) continue;

        TickType_t now = xTaskGetTickCount();

        if (now - last_ping_tick >= pdMS_TO_TICKS(10000)) {
            httpd_ws_frame_t ping = {.type = HTTPD_WS_TYPE_PING, .len = 0};
            for (int i = 0; i < ws_count; i++) {
                httpd_ws_send_frame_async(s_server_ref, ws_fds[i], &ping);
            }
            last_ping_tick = now;
        }

        mic_data_t d;
        mic_get_data(&d);

        fps_cnt++;
        if (now - fps_ts >= pdMS_TO_TICKS(1000)) {
            fps_val = fps_cnt;
            fps_cnt = 0;
            fps_ts  = now;
        }

        int fblen = 0;
        led_get_fb(fb, &fblen);
        if (fblen > LED_MAX_COUNT * 3) fblen = LED_MAX_COUNT * 3;

        int rssi    = wifi_sta_rssi();
        int pos     = 0;
        int max_len = (int)sizeof(msg);

        /* 手写 hex 序列化比 snprintf("%02x") 快约 10 倍，对每帧广播值得 */
        static const char HEX_CHARS[] = "0123456789abcdef";
        pos += snprintf(msg + pos, max_len - pos, "{\"pixels\":\"");
        for (int i = 0; i < fblen && pos + 2 < max_len; i++) {
            msg[pos++] = HEX_CHARS[fb[i] >> 4];
            msg[pos++] = HEX_CHARS[fb[i] & 0xf];
        }

        if (pos + 512 < max_len) {
            pos += snprintf(msg + pos, max_len - pos,
                            "\",\"fps\":%d,\"volume\":%.2f,\"beat\":%.2f,"
                            "\"heap\":%lu,\"rssi\":%d,\"uptime\":%lu,\"bands\":[",
                            fps_val, d.volume, d.beat, (unsigned long)esp_get_free_heap_size(), rssi,
                            (unsigned long)(esp_timer_get_time() / 1000000));
            for (int b = 0; b < MIC_BANDS; b++) {
                if (pos + 10 >= max_len) break;
                pos += snprintf(msg + pos, max_len - pos, "%.2f%s", d.bands[b], (b < MIC_BANDS - 1) ? "," : "");
            }
            pos += snprintf(msg + pos, max_len - pos, "]}");
        }

        httpd_ws_frame_t pkt = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t*)msg,
            .len     = (size_t)pos,
        };

        for (int i = 0; i < ws_count; i++) {
            esp_err_t err = httpd_ws_send_frame_async(s_server_ref, ws_fds[i], &pkt);
            if (err != ESP_OK) {
                /* 这几种错误码表示 socket 已死，必须主动关闭否则 fd 泄漏 */
                if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_NOT_FOUND || err == ESP_ERR_HTTPD_RESP_HDR) {
                    ESP_LOGD(TAG, "ws fd=%d disconnected", ws_fds[i]);
                    httpd_sess_trigger_close(s_server_ref, ws_fds[i]);
                }
            }
        }
    }
}

void ws_push_start(httpd_handle_t server) {
    s_server_ref = server;
    xTaskCreate(push_task, "ws_push", 6144, NULL, 4, NULL);
    ESP_LOGI(TAG, "ws push ready");
}