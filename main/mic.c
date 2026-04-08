#include "mic.h"

#include <driver/i2s_std.h>
#include <dsps_fft2r.h>
#include <dsps_wind.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

static const char* TAG = "mic";

// 频段 bin 边界（对数分布，16kHz / 512pt → bin宽 31.25Hz）
static const int BAND_BINS[MIC_BANDS + 1] = {2, 4, 8, 16, 32, 64, 128, 192, 256};

// 静态缓冲区
static int32_t s_raw[MIC_FFT_SIZE];
static float   s_fft[MIC_FFT_SIZE * 2];
static float   s_wind[MIC_FFT_SIZE];

// AGC 状态
static float s_agc_gain = 1.0f;
#define AGC_TARGET 0.4f

// 节拍历史
#define BEAT_HIST 43
static float s_beat_energy[BEAT_HIST];
static int   s_beat_pos;

// 共享数据
static mic_data_t        s_data;
static SemaphoreHandle_t s_mutex;
static i2s_chan_handle_t s_rx_chan;

// 运行时可调参数（从 settings 同步）
static uint8_t s_agc_mode = 1;
static float   s_gain     = 40.0f;
static uint8_t s_squelch  = 5;
static uint8_t s_smooth   = 100;

// ── AGC ─────────────────────────────────────────────
static float agc_process(float vol) {
    // 先归一化：FFT幅度通常在 0-50 范围，除以20得到大致的0-2.5范围
    float normalized = vol / 20.0f;

    if (s_agc_mode == 0) {
        // 无AGC：直接用增益
        return normalized * s_gain / 100.0f;  // gain范围1-200，映射到0.01-2
    }

    float attack  = (s_agc_mode == 2) ? 0.02f : 0.005f;
    float release = (s_agc_mode == 2) ? 0.005f : 0.001f;

    if (normalized * s_agc_gain > AGC_TARGET)
        s_agc_gain -= attack;
    else
        s_agc_gain += release;

    if (s_agc_gain < 0.1f) s_agc_gain = 0.1f;
    if (s_agc_gain > 10.0f) s_agc_gain = 10.0f;

    return normalized * s_agc_gain;
}

// ── 节拍检测 ─────────────────────────────────────────
static float beat_detect(float energy) {
    float avg = 0;
    for (int i = 0; i < BEAT_HIST; i++) avg += s_beat_energy[i];
    avg /= BEAT_HIST;
    s_beat_energy[s_beat_pos] = energy;
    s_beat_pos                = (s_beat_pos + 1) % BEAT_HIST;
    if (avg < 1e-8f) return 0;
    float ratio = energy / avg;
    float beat  = (ratio - 1.3f) * 2.0f;
    return beat < 0 ? 0 : beat > 1 ? 1 : beat;
}

// ── 麦克风任务 ───────────────────────────────────────
static void mic_task(void* arg) {
    (void)arg;
    float smooth[MIC_BANDS] = {0};
    float peak_decay        = 0;

    while (1) {
        size_t    bytes_read = 0;
        esp_err_t ret =
            i2s_channel_read(s_rx_chan, s_raw, MIC_FFT_SIZE * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(300));
        if (ret != ESP_OK || bytes_read < MIC_FFT_SIZE * sizeof(int32_t)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // 转浮点 + 加 Hann 窗（INMP441：24bit 在高位）
        float raw_energy = 0;
        for (int i = 0; i < MIC_FFT_SIZE; i++) {
            float v          = (float)(s_raw[i] >> 8) * (1.0f / 8388608.0f);
            s_fft[2 * i]     = v * s_wind[i];
            s_fft[2 * i + 1] = 0.0f;
            raw_energy += v * v;
        }
        raw_energy /= MIC_FFT_SIZE;

        dsps_fft2r_fc32(s_fft, MIC_FFT_SIZE);
        dsps_bit_rev2r_fc32(s_fft, MIC_FFT_SIZE);

        // 频段幅度
        float bands[MIC_BANDS];
        float max_band = 0;
        int   max_idx  = 0;
        for (int b = 0; b < MIC_BANDS; b++) {
            float sum = 0;
            int   cnt = BAND_BINS[b + 1] - BAND_BINS[b];
            for (int j = BAND_BINS[b]; j < BAND_BINS[b + 1]; j++) {
                float re = s_fft[2 * j], im = s_fft[2 * j + 1];
                sum += sqrtf(re * re + im * im);
            }
            float val = agc_process(sum / cnt);
            if (val > max_band) {
                max_band = val;
                max_idx  = b;
            }

            float alpha = (val > smooth[b]) ? 0.6f : (s_smooth / 255.0f * 0.3f + 0.05f);
            smooth[b]   = smooth[b] * (1.0f - alpha) + val * alpha;
            bands[b]    = smooth[b] > 1.0f ? 1.0f : smooth[b];
        }

        // 噪声门限
        float squelch_f = s_squelch / 255.0f;
        float volume    = max_band > 1.0f ? 1.0f : max_band;
        if (volume < squelch_f) {
            volume = 0;
            for (int b = 0; b < MIC_BANDS; b++) bands[b] = 0;
        }

        // 峰值 + 节拍
        if (volume > peak_decay)
            peak_decay = volume;
        else
            peak_decay *= 0.996f;

        float beat = beat_detect(raw_energy);
        if (volume < squelch_f) beat = 0;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        for (int b = 0; b < MIC_BANDS; b++) s_data.bands[b] = bands[b];
        s_data.volume        = volume;
        s_data.peak          = peak_decay;
        s_data.beat          = beat;
        s_data.dominant_freq = (float)max_idx;
        xSemaphoreGive(s_mutex);
    }
}

// ── 公共接口 ─────────────────────────────────────────
void mic_init(const settings_t* st) {
    s_mutex = xSemaphoreCreateMutex();
    mic_apply_settings(st);

    dsps_wind_hann_f32(s_wind, MIC_FFT_SIZE);
    dsps_fft2r_init_fc32(NULL, MIC_FFT_SIZE);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear        = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk         = I2S_GPIO_UNUSED,
                .bclk         = st->mic_sck,
                .ws           = st->mic_ws,
                .dout         = I2S_GPIO_UNUSED,
                .din          = st->mic_din,
                .invert_flags = {0},
            },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    xTaskCreatePinnedToCore(mic_task, "mic", 8192, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "init SCK=%d WS=%d DIN=%d", st->mic_sck, st->mic_ws, st->mic_din);
}

void mic_get_data(mic_data_t* out) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_mutex);
}

void mic_apply_settings(const settings_t* st) {
    s_agc_mode = st->agc_mode;
    s_gain     = st->gain;
    s_squelch  = st->squelch;
    s_smooth   = st->fft_smooth;
}
