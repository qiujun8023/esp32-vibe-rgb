#include "settings.h"

#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

#include "config.h"

static const char* TAG = "settings";

static settings_t s = {
    .ssid           = "",
    .pass           = "",
    .ip_mode        = 0,
    .led_gpio       = DEF_LED_GPIO,
    .led_w          = DEF_LED_W,
    .led_h          = DEF_LED_H,
    .led_serpentine = DEF_LED_SERPENTINE,
    .led_start      = DEF_LED_START,
    .brightness     = DEF_BRIGHTNESS,
    .mic_sck        = DEF_MIC_SCK,
    .mic_ws         = DEF_MIC_WS,
    .mic_din        = DEF_MIC_DIN,
    .agc_mode       = DEF_AGC_MODE,
    .gain           = DEF_GAIN,
    .squelch        = DEF_SQUELCH,
    .fft_smooth     = DEF_FFT_SMOOTH,
    .effect         = DEF_EFFECT,
    .palette        = DEF_PALETTE,
    .speed          = DEF_SPEED,
    .intensity      = DEF_INTENSITY,
    .custom1        = DEF_CUSTOM1,
    .custom2        = DEF_CUSTOM2,
    .custom3        = DEF_CUSTOM3,
    .freq_dir       = DEF_FREQ_DIR,
};

void settings_init(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings, using defaults");
        return;
    }
    size_t    len = sizeof(s);
    esp_err_t ret = nvs_get_blob(h, NVS_KEY_SETTINGS, &s, &len);
    nvs_close(h);

    if (ret == ESP_OK && len == sizeof(s)) {
        ESP_LOGI(TAG, "loaded (ssid=%s effect=%d)", s.ssid, s.effect);
    } else {
        ESP_LOGW(TAG, "blob mismatch (len=%d vs %d), using defaults", (int)len, (int)sizeof(s));
    }
}

settings_t* settings_get(void) {
    return &s;
}

void settings_save(void) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_blob(h, NVS_KEY_SETTINGS, &s, sizeof(s)));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "saved");
}

void settings_reset_wifi(void) {
    s.ssid[0] = '\0';
    s.pass[0] = '\0';
    settings_save();
}

void settings_factory_reset(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    esp_restart();
}

bool settings_wifi_configured(void) {
    return s.ssid[0] != '\0';
}
