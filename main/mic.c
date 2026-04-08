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

// 频带 bin 边界（对数分布，覆盖 31Hz ~ 8kHz）
static const int BAND_BINS[MIC_BANDS + 1] = {2, 4, 8, 16, 32, 64, 128, 192, 256};

static int32_t s_raw[MIC_FFT_SIZE];
static float   s_fft[MIC_FFT_SIZE * 2];
static float   s_wind[MIC_FFT_SIZE];

static float s_agc_gain = 1.0f;
#define AGC_TARGET 0.75f

#define BEAT_HIST 43
static float s_beat_energy[BEAT_HIST];
static int   s_beat_pos;

static mic_data_t        s_data;
static SemaphoreHandle_t s_mutex;
static i2s_chan_handle_t s_rx_chan;

static uint8_t s_agc_mode = 1;
static float   s_gain     = 40.0f;
static uint8_t s_squelch  = 5;
static uint8_t s_smooth   = 100;

/**
 * 自动增益控制处理
 */
static float agc_process(float vol) {
    float norm = vol / 20.0f;

    if (s_agc_mode == 0) {
        return norm * s_gain / 100.0f;
    }

    float attack  = (s_agc_mode == 2) ? 0.05f : 0.015f;
    float release = (s_agc_mode == 2) ? 0.01f : 0.003f;

    if (norm * s_agc_gain > AGC_TARGET) {
        s_agc_gain -= attack;
    } else {
        s_agc_gain += release;
    }

    if (s_agc_gain < 0.05f) s_agc_gain = 0.05f;
    if (s_agc_gain > 30.0f) s_agc_gain = 30.0f;

    return norm * s_agc_gain;
}

/**
 * 节拍检测算法
 */
static float beat_detect(float energy) {
    float avg = 0;
    for (int i = 0; i < BEAT_HIST; i++) avg += s_beat_energy[i];
    avg /= BEAT_HIST;

    s_beat_energy[s_beat_pos] = energy;
    s_beat_pos                = (s_beat_pos + 1) % BEAT_HIST;

    if (avg < 1e-8f) return 0;

    float ratio = energy / avg;
    float beat  = (ratio - 1.3f) * 2.0f;
    return (beat < 0) ? 0 : (beat > 1 ? 1 : beat);
}

/**
 * 音频采集与分析任务
 */
static void mic_task(void* arg) {
    float smooth_bands[MIC_BANDS] = {0};
    float band_noise[MIC_BANDS]   = {1e-4f, 1e-4f, 1e-4f, 1e-4f, 1e-4f, 1e-4f, 1e-4f, 1e-4f};
    float band_peak[MIC_BANDS]    = {0};
    float peak_decay              = 0;

    while (1) {
        size_t    bytes_read = 0;
        esp_err_t ret        = i2s_channel_read(s_rx_chan, s_raw, sizeof(s_raw), &bytes_read, pdMS_TO_TICKS(100));

        if (ret != ESP_OK || bytes_read < sizeof(s_raw)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // 预处理：移除直流偏置并应用窗函数
        float raw_energy = 0;
        for (int i = 0; i < MIC_FFT_SIZE; i++) {
            float v          = (float)(s_raw[i] >> 8) * (1.0f / 8388608.0f);
            s_fft[2 * i]     = v * s_wind[i];
            s_fft[2 * i + 1] = 0.0f;
            raw_energy += v * v;
        }
        raw_energy /= MIC_FFT_SIZE;

        // FFT 计算
        dsps_fft2r_fc32(s_fft, MIC_FFT_SIZE);
        dsps_bit_rev2r_fc32(s_fft, MIC_FFT_SIZE);

        float current_bands[MIC_BANDS];
        float max_raw = 0;
        int   max_idx = 0;

        // 频带分析与归一化
        for (int b = 0; b < MIC_BANDS; b++) {
            float sum = 0;
            int   cnt = BAND_BINS[b + 1] - BAND_BINS[b];
            for (int j = BAND_BINS[b]; j < BAND_BINS[b + 1]; j++) {
                float re = s_fft[2 * j], im = s_fft[2 * j + 1];
                sum += sqrtf(re * re + im * im);
            }
            float raw_val = sum / cnt;
            if (raw_val > max_raw) {
                max_raw = raw_val;
                max_idx = b;
            }

            // 底噪自适应
            float noise_alpha = (raw_val < band_noise[b]) ? 0.05f : 0.0001f;
            band_noise[b]     = band_noise[b] * (1.0f - noise_alpha) + raw_val * noise_alpha;

            float signal = raw_val - band_noise[b];
            if (signal < 0.0f) signal = 0.0f;

            // 峰值跟踪
            float peak_min = band_noise[b] * 4.0f;
            if (signal > band_peak[b]) {
                band_peak[b] = signal;
            } else {
                band_peak[b] *= 0.992f;
                if (band_peak[b] < peak_min) band_peak[b] = peak_min;
            }

            float val = (band_peak[b] > 1e-8f) ? (signal / band_peak[b]) : 0.0f;
            if (val > 1.0f) val = 1.0f;

            // 时域平滑
            float alpha      = (val > smooth_bands[b]) ? 0.6f : (s_smooth / 255.0f * 0.3f + 0.05f);
            smooth_bands[b]  = smooth_bands[b] * (1.0f - alpha) + val * alpha;
            current_bands[b] = smooth_bands[b];
        }

        float volume = agc_process(max_raw);
        if (volume > 1.0f) volume = 1.0f;

        float squelch_f = (float)s_squelch / 255.0f;
        if (volume < squelch_f) {
            volume = 0;
            for (int b = 0; b < MIC_BANDS; b++) current_bands[b] = 0;
        }

        if (volume > peak_decay)
            peak_decay = volume;
        else
            peak_decay *= 0.996f;

        float beat = beat_detect(raw_energy);
        if (volume < squelch_f) beat = 0;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memcpy(s_data.bands, current_bands, sizeof(current_bands));
        s_data.volume        = volume;
        s_data.peak          = peak_decay;
        s_data.beat          = beat;
        s_data.dominant_freq = (float)max_idx;
        xSemaphoreGive(s_mutex);
    }
}

void mic_init(const settings_t* st) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }

    mic_apply_settings(st);

    dsps_wind_hann_f32(s_wind, MIC_FFT_SIZE);
    dsps_fft2r_init_fc32(NULL, MIC_FFT_SIZE);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear        = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create i2s channel: %s", esp_err_to_name(err));
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = st->mic_sck,
                .ws   = st->mic_ws,
                .dout = I2S_GPIO_UNUSED,
                .din  = st->mic_din,
            },
    };

    if (i2s_channel_init_std_mode(s_rx_chan, &std_cfg) != ESP_OK || i2s_channel_enable(s_rx_chan) != ESP_OK) {
        ESP_LOGE(TAG, "failed to init i2s channel");
        return;
    }

    xTaskCreatePinnedToCore(mic_task, "mic", 8192, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "mic init ok, sck: %d, ws: %d, din: %d", st->mic_sck, st->mic_ws, st->mic_din);
}

void mic_get_data(mic_data_t* out) {
    if (!out || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(out, &s_data, sizeof(mic_data_t));
    xSemaphoreGive(s_mutex);
}

void mic_apply_settings(const settings_t* st) {
    s_agc_mode = st->agc_mode;
    s_gain     = st->gain;
    s_squelch  = st->squelch;
    s_smooth   = st->fft_smooth;
}