/**
 * @file mic.c
 * @brief I2S 麦克风采集、FFT 频谱分析、节拍检测、自动增益控制
 */

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

#define SAMPLE_RATE 16000

/* 频带边界（FFT bin 索引），覆盖约 60 Hz - 4 kHz */
static const int BAND_BINS[MIC_BANDS + 1] = {2, 4, 8, 16, 32, 64, 128, 192, 256};

/* 粉红噪声补偿系数 */
static const float PINK_NOISE_COMP[MIC_BANDS] = {1.85f, 1.86f, 1.88f, 1.94f, 2.08f, 2.38f, 3.15f, 5.12f};

/* Blackman-Harris 窗函数系数 */
#define BH_A0 0.35875f
#define BH_A1 0.48829f
#define BH_A2 0.14128f
#define BH_A3 0.01168f

#define BEAT_HIST 43

static int32_t           s_raw[MIC_FFT_SIZE];
static float             s_fft[MIC_FFT_SIZE * 2];
static float             s_wind[MIC_FFT_SIZE];
static float             s_beat_energy[BEAT_HIST];
static int               s_beat_pos;
static mic_data_t        s_data;
static SemaphoreHandle_t s_mutex;
static i2s_chan_handle_t s_rx_chan;

/**
 * @brief 滤波器状态：噪声底、峰值保持、平滑值、AGC
 */
typedef struct {
    float band_noise[MIC_BANDS];
    float band_peak[MIC_BANDS];
    float smooth_bands[MIC_BANDS];
    float smooth_vol;
    float peak_decay;
    float vol_noise;
    float agc_level;    /* AGC 自适应增益基准 */
    float agc_peak_avg; /* AGC 峰值平均值 */
    bool  need_reset;
} mic_filter_t;

static mic_filter_t s_filter;
static uint8_t      s_squelch  = 5;
static uint8_t      s_smooth   = 100;
static uint8_t      s_agc_mode = 1;
static float        s_gain     = 15.0f;

/**
 * @brief 重置滤波器状态
 */
static void filter_reset(mic_filter_t* f) {
    for (int i = 0; i < MIC_BANDS; i++) {
        f->band_noise[i]   = 1e-4f;
        f->band_peak[i]    = 0.0f;
        f->smooth_bands[i] = 0.0f;
    }
    f->smooth_vol   = 0.0f;
    f->peak_decay   = 0.0f;
    f->vol_noise    = 1e-4f;
    f->agc_level    = 1.0f;
    f->agc_peak_avg = 0.0f;
    f->need_reset   = false;
}

/**
 * @brief 生成 Blackman-Harris 窗函数
 */
static void generate_blackman_harris_window(float* window, int size) {
    float inv = 1.0f / (size - 1);
    for (int i = 0; i < size; i++) {
        float r = i * inv;
        window[i] =
            BH_A0 - BH_A1 * cosf(2.0f * M_PI * r) + BH_A2 * cosf(4.0f * M_PI * r) - BH_A3 * cosf(6.0f * M_PI * r);
    }
}

/**
 * @brief FFT 主峰检测（抛物线插值）
 */
static void find_major_peak(float* fft_data, int fft_size, int sample_rate, float* out_freq, float* out_mag) {
    float max_mag_sq = 0.0f;
    int   max_bin    = 1;
    int   half       = fft_size / 2;

    for (int i = 1; i < half; i++) {
        float re = fft_data[2 * i], im = fft_data[2 * i + 1];
        float mag_sq = re * re + im * im;
        if (mag_sq > max_mag_sq) {
            max_mag_sq = mag_sq;
            max_bin    = i;
        }
    }

    float max_mag = sqrtf(max_mag_sq);
    float y1 = 0, y2 = max_mag, y3 = 0;

    if (max_bin > 1) {
        float re1 = fft_data[2 * (max_bin - 1)];
        float im1 = fft_data[2 * (max_bin - 1) + 1];
        y1        = sqrtf(re1 * re1 + im1 * im1);
    }
    if (max_bin < half - 1) {
        float re3 = fft_data[2 * (max_bin + 1)];
        float im3 = fft_data[2 * (max_bin + 1) + 1];
        y3        = sqrtf(re3 * re3 + im3 * im3);
    }

    float denom = y1 - 2.0f * y2 + y3;
    float delta = (fabsf(denom) > 1e-6f) ? (0.5f * (y1 - y3) / denom) : 0.0f;

    *out_freq = (max_bin + delta) * sample_rate / fft_size;
    *out_mag  = max_mag;
}

/**
 * @brief 节拍检测（基于能量方差）
 */
static float beat_detect(float energy) {
    float avg = 0;
    for (int i = 0; i < BEAT_HIST; i++) avg += s_beat_energy[i];
    avg /= BEAT_HIST;

    s_beat_energy[s_beat_pos] = energy;
    s_beat_pos                = (s_beat_pos + 1) % BEAT_HIST;

    if (avg < 1e-8f) return 0;
    float beat = (energy / avg - 1.3f) * 2.0f;
    return (beat < 0) ? 0 : (beat > 1 ? 1 : beat);
}

/**
 * @brief AGC 增益计算
 *
 * @param peak 当前帧峰值幅度
 * @return 应用 AGC 后的增益系数
 */
static float compute_agc_gain(float peak) {
    if (s_agc_mode == 0) {
        /* 手动增益模式 */
        return s_gain;
    }

    /* AGC 目标：将峰值稳定在约 0.3 - 0.5 范围 */
    float target    = 0.4f;
    float agc_speed = (s_agc_mode == 2) ? 0.02f : 0.01f; /* 强力 AGC 更快 */

    /* 计算峰值平均值（慢速） */
    s_filter.agc_peak_avg = s_filter.agc_peak_avg * 0.95f + peak * 0.05f;

    /* 根据峰值平均值调整增益基准 */
    if (s_filter.agc_peak_avg > target * 1.5f) {
        /* 信号过强，降低增益 */
        s_filter.agc_level -= agc_speed;
        if (s_filter.agc_level < 1.0f) s_filter.agc_level = 1.0f;
    } else if (s_filter.agc_peak_avg < target * 0.5f) {
        /* 信号过弱，提高增益 */
        s_filter.agc_level += agc_speed * 0.5f;
        if (s_filter.agc_level > s_gain * 2.0f) s_filter.agc_level = s_gain * 2.0f;
    }

    return s_filter.agc_level;
}

/**
 * @brief 麦克风采集与处理任务
 */
static void mic_task(void* arg) {
    filter_reset(&s_filter);

    while (1) {
        if (s_filter.need_reset) {
            filter_reset(&s_filter);
        }

        size_t    bytes_read = 0;
        esp_err_t ret        = i2s_channel_read(s_rx_chan, s_raw, sizeof(s_raw), &bytes_read, pdMS_TO_TICKS(100));
        if (ret != ESP_OK || bytes_read < sizeof(s_raw)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* 加窗并计算原始能量 */
        float raw_energy = 0;
        float peak_val   = 0;
        for (int i = 0; i < MIC_FFT_SIZE; i++) {
            float v          = (float)(s_raw[i] >> 8) * (1.0f / 8388608.0f);
            float wv         = v * s_wind[i];
            s_fft[2 * i]     = wv;
            s_fft[2 * i + 1] = 0.0f;
            raw_energy += v * v;
            if (fabsf(v) > peak_val) peak_val = fabsf(v);
        }
        raw_energy /= MIC_FFT_SIZE;

        /* 应用增益 */
        float gain = compute_agc_gain(peak_val);
        for (int i = 0; i < MIC_FFT_SIZE; i++) {
            s_fft[2 * i] *= gain;
        }

        dsps_fft2r_fc32(s_fft, MIC_FFT_SIZE);
        dsps_bit_rev2r_fc32(s_fft, MIC_FFT_SIZE);

        float major_peak_freq, major_peak_mag;
        find_major_peak(s_fft, MIC_FFT_SIZE, SAMPLE_RATE, &major_peak_freq, &major_peak_mag);

        float current_bands[MIC_BANDS];
        float max_raw = 0;
        int   max_idx = 0;

        uint8_t local_smooth = s_smooth;

        for (int b = 0; b < MIC_BANDS; b++) {
            float sum = 0;
            int   cnt = BAND_BINS[b + 1] - BAND_BINS[b];
            for (int j = BAND_BINS[b]; j < BAND_BINS[b + 1]; j++) {
                float re = s_fft[2 * j], im = s_fft[2 * j + 1];
                sum += sqrtf(re * re + im * im);
            }

            float raw_val = (sum / cnt) * PINK_NOISE_COMP[b];
            if (raw_val > max_raw) {
                max_raw = raw_val;
                max_idx = b;
            }

            /* 自适应噪声底 */
            float noise_alpha      = (raw_val < s_filter.band_noise[b]) ? 0.05f : 0.0001f;
            s_filter.band_noise[b] = s_filter.band_noise[b] * (1.0f - noise_alpha) + raw_val * noise_alpha;

            float signal = raw_val - s_filter.band_noise[b];
            if (signal < 0.0f) signal = 0.0f;

            /* 峰值保持 */
            float peak_min = s_filter.band_noise[b] * 4.0f;
            if (signal > s_filter.band_peak[b]) {
                s_filter.band_peak[b] = signal;
            } else {
                s_filter.band_peak[b] *= 0.992f;
                if (s_filter.band_peak[b] < peak_min) s_filter.band_peak[b] = peak_min;
            }

            float val = (s_filter.band_peak[b] > 1e-8f) ? (signal / s_filter.band_peak[b]) : 0.0f;
            if (val > 1.0f) val = 1.0f;

            /* 平滑处理 */
            float alpha              = (val > s_filter.smooth_bands[b]) ? 0.6f : (local_smooth / 255.0f * 0.3f + 0.05f);
            s_filter.smooth_bands[b] = s_filter.smooth_bands[b] * (1.0f - alpha) + val * alpha;
            current_bands[b]         = s_filter.smooth_bands[b];
        }

        /* 音量计算 */
        float vol_noise_alpha = (max_raw < s_filter.vol_noise) ? 0.05f : 0.005f;
        s_filter.vol_noise    = s_filter.vol_noise * (1.0f - vol_noise_alpha) + max_raw * vol_noise_alpha;

        float volume = (max_raw - s_filter.vol_noise * 1.5f) * 4.0f;
        if (volume < 0) volume = 0;
        if (volume > 1.0f) volume = 1.0f;

        float vol_alpha     = (volume > s_filter.smooth_vol) ? 0.7f : 0.2f;
        s_filter.smooth_vol = s_filter.smooth_vol * (1.0f - vol_alpha) + volume * vol_alpha;

        if (s_filter.smooth_vol > s_filter.peak_decay) {
            s_filter.peak_decay = s_filter.smooth_vol;
        } else {
            s_filter.peak_decay *= 0.98f;
        }

        /* 噪声门限 */
        float squelch_f  = (float)s_squelch / 255.0f * 0.15f;
        float out_volume = (s_filter.smooth_vol > squelch_f) ? s_filter.smooth_vol : 0;
        if (out_volume == 0) {
            for (int b = 0; b < MIC_BANDS; b++) current_bands[b] = 0;
        }

        float beat = beat_detect(raw_energy);
        if (out_volume == 0) beat = 0;

        /* 写入共享数据 */
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memcpy(s_data.bands, current_bands, sizeof(current_bands));
        s_data.volume        = out_volume;
        s_data.peak          = s_filter.peak_decay;
        s_data.beat          = beat;
        s_data.dominant_freq = (float)max_idx;
        s_data.major_peak    = major_peak_freq;
        s_data.major_mag     = major_peak_mag;
        xSemaphoreGive(s_mutex);
    }
}

void mic_init(const settings_t* st) {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }

    mic_apply_settings(st);

    generate_blackman_harris_window(s_wind, MIC_FFT_SIZE);
    dsps_fft2r_init_fc32(NULL, MIC_FFT_SIZE);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear        = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create i2s channel: %s", esp_err_to_name(err));
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
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
    s_squelch           = st->squelch;
    s_smooth            = st->fft_smooth;
    s_agc_mode          = st->agc_mode;
    s_gain              = st->gain;
    s_filter.need_reset = true;
}