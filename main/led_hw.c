// LED 硬件驱动层：RMT/WS2812 初始化、亮度缩放、刷新
// 依赖 IDF led_strip 组件

#include "led_priv.h"

#include <esp_log.h>
#include <led_strip.h>
#include <string.h>

#include "settings.h"

static const char* TAG = "led";

// ── 模块状态定义 ──────────────────────────────────────────────────────────────
led_strip_handle_t s_strip = NULL;

static inline uint8_t scale_brightness(uint8_t v) {
    return (uint8_t)((uint32_t)v * s_brightness / 255);
}

// ── 初始化 ────────────────────────────────────────────────────────────────────
void led_init(const settings_t* st) {
    s_w          = st->led_w ? st->led_w : 8;
    s_h          = st->led_h ? st->led_h : 8;
    s_serpentine = st->led_serpentine;
    s_start      = st->led_start;
    s_rotation   = st->led_rotation;
    s_brightness = st->brightness;

    int count = s_w * s_h;
    if (count > LED_MAX_COUNT) {
        ESP_LOGE(TAG, "led count %d exceeds max %d", count, LED_MAX_COUNT);
        count = LED_MAX_COUNT;
    }

    memset(s_fb, 0, sizeof(s_fb));
    ledmap_rebuild();

    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = st->led_gpio,
        .max_leds         = count,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma    = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create led strip: %s", esp_err_to_name(err));
        return;
    }
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "led init ok, gpio: %d, size: %dx%d", st->led_gpio, s_w, s_h);
}

// ── 运行时更新设置 ────────────────────────────────────────────────────────────
void led_apply_settings(const settings_t* st) {
    s_brightness = st->brightness;
    s_serpentine = st->led_serpentine;
    s_start      = st->led_start;
    s_rotation   = st->led_rotation;
    int new_w    = st->led_w ? st->led_w : 8;
    int new_h    = st->led_h ? st->led_h : 8;

    if (new_w <= 0 || new_h <= 0 || new_w * new_h > LED_MAX_COUNT) {
        ESP_LOGE(TAG, "invalid led size %dx%d", new_w, new_h);
        return;
    }
    s_w = new_w;
    s_h = new_h;
    ledmap_rebuild();
}

// ── 清屏 ──────────────────────────────────────────────────────────────────────
void led_clear(void) {
    memset(s_fb, 0, sizeof(s_fb));
    if (s_strip) led_strip_clear(s_strip);
}

// ── 刷新输出（最后进行物理映射与亮度缩放） ────────────────────────────────────
void led_flush(void) {
    if (!s_strip) return;

    led_strip_clear(s_strip);

    int count = s_w * s_h;
    for (int y = 0; y < s_h; y++) {
        for (int x = 0; x < s_w; x++) {
            int phy_idx = s_lookup[y * s_w + x];
            if (phy_idx >= 0 && phy_idx < count) {
                int log_base = (y * s_w + x) * 3;
                led_strip_set_pixel(s_strip, phy_idx,
                                    scale_brightness(s_fb[log_base]),
                                    scale_brightness(s_fb[log_base + 1]),
                                    scale_brightness(s_fb[log_base + 2]));
            }
        }
    }
    led_strip_refresh(s_strip);
}

// ── 提供给Web测试布线的物理硬件后门 ──────────────────────────────────────
void led_hw_test_pixel(int idx, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_strip) return;
    led_strip_clear(s_strip);
    if (idx >= 0 && idx < s_w * s_h) {
        led_strip_set_pixel(s_strip, idx, r, g, b);
    }
    led_strip_refresh(s_strip);
}
