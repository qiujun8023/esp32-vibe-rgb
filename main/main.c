#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "effects.h"
#include "led.h"
#include "mic.h"
#include "settings.h"
#include "wifi_ctrl.h"
#include "wifi_prov.h"

static const char* TAG = "main";

static void effect_task(void* arg) {
    (void)arg;
    mic_data_t data;
    TickType_t last = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(33));  // ~30fps
        mic_get_data(&data);
        effects_update(&data, settings_get());
    }
}

static void boot_animation(void) {
    int w = led_width(), h = led_height();
    for (int step = 0; step < 24; step++) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint16_t hue = (uint16_t)((x + y + step) * 15) % 360;
                uint8_t  v   = step < 12 ? step * 20 : (23 - step) * 20;
                led_set_pixel_hsv(x, y, hue, 255, v);
            }
        led_flush();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    led_clear();
    led_flush();
}

static void prov_indicator(void) {
    static int step = 0;
    int        w = led_width(), h = led_height();
    led_clear();
    int col = (step++) % w;
    for (int y = 0; y < h; y++) led_set_pixel_hsv(col, y, 220, 200, 120);
    led_flush();
}

void app_main(void) {
    ESP_LOGI(TAG, "===== ESP32-LED starting =====");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    settings_init();
    settings_t* s = settings_get();

    led_init(s);
    effects_init();
    effects_set_mode(s->effect);

    boot_animation();

    if (!settings_wifi_configured()) {
        ESP_LOGI(TAG, "no wifi, starting provisioning");
        for (int i = 0; i < 20; i++) {
            prov_indicator();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        wifi_prov_start_ap();  // 不返回
    }

    mic_init(s);
    wifi_ctrl_init();

    xTaskCreatePinnedToCore(effect_task, "effect", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "===== system ready =====");
}
