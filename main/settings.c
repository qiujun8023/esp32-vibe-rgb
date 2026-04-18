#include "settings.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

#include "config.h"

static const char* TAG = "settings";

static const uint8_t DEFAULT_PARAMS[EFFECT_COUNT][3] = DEFAULT_EFFECT_PARAMS;

static SemaphoreHandle_t s_mutex = NULL;
static settings_t        s;
/* 最近一次成功写入 NVS 的内容;配合 memcmp 跳过无变化的写,减少 flash 擦写次数 */
static settings_t        s_last_saved;
static bool              s_last_saved_valid = false;

static void settings_validate(settings_t* cfg) {
    if (cfg->led_w == 0 || cfg->led_w > 64) cfg->led_w = DEF_LED_W;
    if (cfg->led_h == 0 || cfg->led_h > 64) cfg->led_h = DEF_LED_H;
    if (cfg->brightness == 0) cfg->brightness = DEF_BRIGHTNESS;
    if (cfg->effect >= EFFECT_COUNT) cfg->effect = 0;
    if (cfg->gain < 1.0f || cfg->gain > 200.0f) cfg->gain = DEF_GAIN;
    if (cfg->agc_mode > 2) cfg->agc_mode = DEF_AGC_MODE;
}

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

    memcpy(s.effect_params, DEFAULT_PARAMS, sizeof(s.effect_params));

    s.cfg_version = SETTINGS_VERSION;
}

void settings_init(void) {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    settings_set_defaults();

    nvs_handle_t h;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "using defaults");
        return;
    }

    size_t    len = sizeof(s);
    esp_err_t ret = nvs_get_blob(h, NVS_KEY_SETTINGS, &s, &len);
    nvs_close(h);

    if (ret == ESP_OK && len > 0) {
        /* blob 比当前 struct 小说明是旧版本,缺失字段保留 settings_set_defaults 的值 */
        if (len < sizeof(s)) s.cfg_version = 0;
        ESP_LOGI(TAG, "settings loaded v%d", s.cfg_version);

        if (s.cfg_version < SETTINGS_VERSION) {
            ESP_LOGW(TAG, "upgrading settings v%d -> v%d", s.cfg_version, SETTINGS_VERSION);

            /* v1 -> v2: 新增 effect_params 数组 */
            if (s.cfg_version < 2) {
                memcpy(s.effect_params, DEFAULT_PARAMS, sizeof(s.effect_params));
            }

            s.cfg_version = SETTINGS_VERSION;
            settings_validate(&s);
            settings_save();
        }
    } else {
        ESP_LOGW(TAG, "nvs read failed: %s, using defaults", esp_err_to_name(ret));
        settings_set_defaults();
    }

    settings_validate(&s);

    settings_effect_load_params(s.effect);

    /* 把启动时的 NVS 状态作为"已保存"基线,避免无变更时重复写 flash */
    s_last_saved       = s;
    s_last_saved_valid = true;
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
    /* 先拷贝到栈快照再放锁,flash 擦写可能耗时数十 ms,持锁会阻塞其他任务 */
    settings_t snap;
    settings_lock();
    snap = s;
    settings_unlock();

    if (s_last_saved_valid && memcmp(&snap, &s_last_saved, sizeof(snap)) == 0) {
        return;
    }

    nvs_handle_t h;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs open failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(h, NVS_KEY_SETTINGS, &snap, sizeof(snap));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_last_saved       = snap;
        s_last_saved_valid = true;
        ESP_LOGI(TAG, "settings saved");
    } else {
        ESP_LOGE(TAG, "settings save failed: %s", esp_err_to_name(err));
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

void settings_effect_load_params(uint8_t effect_id) {
    if (effect_id >= EFFECT_COUNT) return;
    settings_lock();
    s.custom1 = s.effect_params[effect_id][0];
    s.custom2 = s.effect_params[effect_id][1];
    s.custom3 = s.effect_params[effect_id][2];
    settings_unlock();
}
