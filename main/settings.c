/**
 * @file settings.c
 * @brief 全局配置管理：NVS 存储、线程安全访问、版本迁移
 */

#include "settings.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

#include "config.h"
#include "effects.h"

static const char* TAG = "settings";

static SemaphoreHandle_t s_mutex = NULL;
static settings_t        s;

/**
 * @brief 验证并修正配置字段范围
 */
static void settings_validate(settings_t* cfg) {
    if (cfg->led_w == 0 || cfg->led_w > 64) cfg->led_w = DEF_LED_W;
    if (cfg->led_h == 0 || cfg->led_h > 64) cfg->led_h = DEF_LED_H;
    if (cfg->brightness == 0) cfg->brightness = DEF_BRIGHTNESS;
    if (cfg->effect >= EFFECT_COUNT) cfg->effect = 0;
    if (cfg->gain < 1.0f || cfg->gain > 200.0f) cfg->gain = DEF_GAIN;
    if (cfg->agc_mode > 2) cfg->agc_mode = DEF_AGC_MODE;
}

/**
 * @brief 初始化默认配置
 */
static void settings_set_defaults(void) {
    memset(&s, 0, sizeof(s));
    s.led_gpio       = DEF_LED_GPIO;
    s.led_w          = DEF_LED_W;
    s.led_h          = DEF_LED_H;
    s.led_serpentine = DEF_LED_SERPENTINE;
    s.led_start      = DEF_LED_START;
    s.led_rotation   = DEF_LED_ROTATION;
    s.brightness     = DEF_BRIGHTNESS;
    s.mic_sck        = DEF_MIC_SCK;
    s.mic_ws         = DEF_MIC_WS;
    s.mic_din        = DEF_MIC_DIN;
    s.agc_mode       = DEF_AGC_MODE;
    s.gain           = DEF_GAIN;
    s.squelch        = DEF_SQUELCH;
    s.fft_smooth     = DEF_FFT_SMOOTH;
    s.effect         = DEF_EFFECT;
    s.speed          = DEF_SPEED;
    s.intensity      = DEF_INTENSITY;
    s.custom1        = DEF_CUSTOM1;
    s.custom2        = DEF_CUSTOM2;
    s.custom3        = DEF_CUSTOM3;
    s.cfg_version    = SETTINGS_VERSION;
}

void settings_init(void) {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }

    settings_set_defaults();

    nvs_handle_t h;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings, using defaults");
        return;
    }

    size_t    len = sizeof(s);
    esp_err_t ret = nvs_get_blob(h, NVS_KEY_SETTINGS, &s, &len);
    nvs_close(h);

    if (ret == ESP_OK && len >= sizeof(s) - sizeof(s.cfg_version)) {
        if (len < sizeof(s)) s.cfg_version = 0;
        ESP_LOGI(TAG, "settings loaded v%d", s.cfg_version);

        if (s.cfg_version < SETTINGS_VERSION) {
            ESP_LOGW(TAG, "config version mismatch, upgrading");
            s.cfg_version = SETTINGS_VERSION;
            settings_validate(&s);
            settings_save();
        }
    } else {
        ESP_LOGW(TAG, "nvs read failed, using defaults");
        settings_set_defaults();
    }

    settings_validate(&s);
}

settings_t* settings_get(void) {
    return &s;
}

void settings_lock(void) {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void settings_unlock(void) {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void settings_copy(settings_t* out) {
    settings_lock();
    *out = s;
    settings_unlock();
}

void settings_save(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        settings_lock();
        nvs_set_blob(h, NVS_KEY_SETTINGS, &s, sizeof(s));
        settings_unlock();
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "settings saved");
    } else {
        ESP_LOGE(TAG, "failed to open nvs for writing");
    }
}

void settings_factory_reset(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "factory reset, restarting");
    esp_restart();
}

bool settings_wifi_configured(void) {
    settings_lock();
    bool configured = (s.ssid[0] != '\0' && strlen(s.ssid) >= 2);
    settings_unlock();
    return configured;
}