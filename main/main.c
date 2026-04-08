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

/**
 * 灯效渲染任务，运行在 Core 0
 */
static void effect_task(void* arg) {
    mic_data_t data;
    TickType_t last = xTaskGetTickCount();

    while (1) {
        // 维持约 30fps 渲染帧率
        vTaskDelayUntil(&last, pdMS_TO_TICKS(33));

        mic_get_data(&data);

        settings_lock();
        settings_t* s = settings_get();
        effects_update(&data, s);
        settings_unlock();
    }
}

/**
 * 开机动画，展示彩虹渐变效果确认 LED 硬件正常
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
 * 配网指示动画任务
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

void app_main(void) {
    ESP_LOGI(TAG, "system starting");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs partition corrupted, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 加载配置
    settings_init();
    settings_t* s = settings_get();

    // 初始化 LED 和特效引擎
    led_init(s);
    effects_init();
    effects_set_mode(s->effect);

    // 执行开机动画
    boot_animation();

    // 检查 WiFi 配置状态
    if (!settings_wifi_configured()) {
        ESP_LOGI(TAG, "wifi not configured, entering provisioning mode");
        xTaskCreate(prov_led_task, "prov_led", 2048, NULL, 4, NULL);
        wifi_prov_start_ap();
    }

    // 正常运行模式
    ESP_LOGI(TAG, "entering normal mode");
    mic_init(s);
    wifi_ctrl_init();

    // 启动渲染任务 (Core 0)
    xTaskCreatePinnedToCore(effect_task, "render", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "system ready");
}