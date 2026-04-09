/**
 * @file main.c
 * @brief ESP32 Vibe RGB 主入口
 */
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "config.h"
#include "effects.h"
#include "led.h"
#include "mic.h"
#include "settings.h"
#include "net/net_init.h"
#include "net/wifi_sta.h"
#include "net/wifi_prov.h"
#include "net/http_server.h"

static const char* TAG = "main";

/**
 * @brief 灯效渲染任务（Core 0，约30fps）
 *
 * 使用 settings_copy 快照避免持锁渲染，保证实时性。
 */
static void effect_task(void* arg) {
    mic_data_t data;
    TickType_t last = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(33));
        mic_get_data(&data);
        settings_t snap;
        settings_copy(&snap);
        effects_update(&data, &snap);
        if (!effects_is_paused()) {
            led_flush();
        }
    }
}

/**
 * @brief 开机动画
 */
static void boot_animation(void) {
    int w = led_width(), h = led_height();
    for (int step = 0; step < 24; step++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint16_t hue = (uint16_t)((x + y + step) * 15) % 360;
                uint8_t  v   = (step < 12) ? (step * 20) : ((23 - step) * 20);
                led_set_pixel_hsv(x, y, hue, 255, v);
            }
        }
        led_flush();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    led_clear();
    led_flush();
}

/**
 * @brief 配网指示动画任务（扫列蓝光）
 */
static void prov_led_task(void* arg) {
    int step = 0;
    while (1) {
        int w = led_width(), h = led_height();
        led_clear();
        int col = (step++) % w;
        for (int y = 0; y < h; y++) {
            led_set_pixel_hsv(col, y, 220, 200, 120);
        }
        led_flush();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief 应用入口
 */
void app_main(void) {
    ESP_LOGI(TAG, "system starting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs partition corrupted, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    settings_init();
    net_init();

    settings_t snap;
    settings_copy(&snap);
    led_init(&snap);
    effects_init();
    effects_set_mode(snap.effect);

    boot_animation();

    if (!settings_wifi_configured()) {
        ESP_LOGI(TAG, "wifi not configured, entering provisioning mode");
        xTaskCreate(prov_led_task, "prov_led", 2048, NULL, 4, NULL);
        wifi_prov_start_ap();
    }

    ESP_LOGI(TAG, "entering normal mode");

    settings_copy(&snap);
    mic_init(&snap);

    wifi_sta_init(&snap);
    if (!wifi_sta_wait_connected(WIFI_STA_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "wifi connection failed, clearing credentials");
        settings_lock();
        settings_t* s = settings_get();
        s->ssid[0] = '\0';
        s->pass[0] = '\0';
        settings_unlock();
        settings_save();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    http_server_start();

    xTaskCreatePinnedToCore(effect_task, "render", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "system ready");
}
