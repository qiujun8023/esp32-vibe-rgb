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

/* FFT bin 索引,对数分布覆盖约 60 Hz - 4 kHz */
static const int BAND_BINS[MIC_BANDS + 1] = {2, 4, 8, 16, 32, 64, 128, 192, 256};

/* 粉红噪声补偿:麦克风本底 1/f 特性下高频权重偏低,按频带乘此系数拉平 */
static const float PINK_NOISE_COMP[MIC_BANDS] = {1.85f, 1.86f, 1.88f, 1.94f, 2.08f, 2.38f, 3.15f, 5.12f};

/* Blackman-Harris 窗函数系数 */
#define BH_A0 0.35875f
#define BH_A1 0.48829f
#define BH_A2 0.14128f
#define BH_A3 0.01168f

/* 节拍检测历史长度,~1 秒 @ 43 fps */
#define BEAT_HIST 43

/* esp-dsp fft2r 要求实虚交织缓冲 16 字节对齐 */
static int32_t s_raw[MIC_FFT_SIZE];
static float   s_fft[MIC_FFT_SIZE * 2] __attribute__((aligned(16)));
static float   s_wind[MIC_FFT_SIZE] __attribute__((aligned(16)));
static float             s_beat_energy[BEAT_HIST];
static int               s_beat_pos;
static mic_data_t        s_data;
static SemaphoreHandle_t s_mutex;
static i2s_chan_handle_t s_rx_chan;

typedef struct {
    float band_noise[MIC_BANDS];
    float band_peak[MIC_BANDS];
    float smooth_bands[MIC_BANDS];
    float smooth_vol;
    float peak_decay;
    float vol_noise;
    float agc_level;
    float agc_peak_avg;
    bool  need_reset;
} mic_filter_t;

static mic_filter_t s_filter;
static uint8_t      s_squelch  = 5;
static uint8_t      s_smooth   = 100;
static uint8_t      s_agc_mode = 1;
static float        s_gain     = 15.0f;

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

static void generate_blackman_harris_window(float* window, int size) {
    float inv = 1.0f / (size - 1);
    for (int i = 0; i < size; i++) {
        float r = i * inv;
        window[i] =
            BH_A0 - BH_A1 * cosf(2.0f * M_PI * r) + BH_A2 * cosf(4.0f * M_PI * r) - BH_A3 * cosf(6.0f * M_PI * r);
    }
}

/* 抛物线插值定位主峰,精度优于 bin 宽度 */
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

/* 当前能量相对历史均值的突跳强度映射到 [0,1] */
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

static float compute_agc_gain(float peak) {
    if (s_agc_mode == 0) {
        return s_gain;
    }

    /* 目标:把峰值稳定在 0.3-0.5 区间 */
    float target    = 0.4f;
    /* mode 2 为强力 AGC,调节更快 */
    float agc_speed = (s_agc_mode == 2) ? 0.02f : 0.01f;

    s_filter.agc_peak_avg = s_filter.agc_peak_avg * 0.95f + peak * 0.05f;

    if (s_filter.agc_peak_avg > target * 1.5f) {
        s_filter.agc_level -= agc_speed;
        if (s_filter.agc_level < 1.0f) s_filter.agc_level = 1.0f;
    } else if (s_filter.agc_peak_avg < target * 0.5f) {
        s_filter.agc_level += agc_speed * 0.5f;
        /* 上限 2*s_gain 防止安静场景把底噪放大成毛刺 */
        if (s_filter.agc_level > s_gain * 2.0f) s_filter.agc_level = s_gain * 2.0f;
    }

    return s_filter.agc_level;
}

static void mic_task(void* arg) {
    filter_reset(&s_filter);

    while (1) {
        if (s_filter.need_reset) {
            filter_reset(&s_filter);
        }

        static int s_drop_count = 0;
        size_t    bytes_read = 0;
        esp_err_t ret        = i2s_channel_read(s_rx_chan, s_raw, sizeof(s_raw), &bytes_read, pdMS_TO_TICKS(100));
        if (ret != ESP_OK || bytes_read < sizeof(s_raw)) {
            /* 连续丢帧超过阈值才记录，避免短暂抖动刷屏 */
            if (++s_drop_count >= 100) {
                ESP_LOGW(TAG, "i2s read stall: %d frames dropped", s_drop_count);
                s_drop_count = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        s_drop_count = 0;

        /* I2S 返回 32-bit,有效数据在高 24 位,>>8 后按 24-bit 定点归一化 */
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

            /* 非对称追踪:值低于底噪时快速下跟,高于时极慢上爬,避免长信号被误认为背景 */
            float noise_alpha      = (raw_val < s_filter.band_noise[b]) ? 0.05f : 0.0001f;
            s_filter.band_noise[b] = s_filter.band_noise[b] * (1.0f - noise_alpha) + raw_val * noise_alpha;

            float signal = raw_val - s_filter.band_noise[b];
            if (signal < 0.0f) signal = 0.0f;

            /* 峰值保持:给归一化提供稳定分母,衰减太慢会钝化响应,太快会抖 */
            float peak_min = s_filter.band_noise[b] * 4.0f;
            if (signal > s_filter.band_peak[b]) {
                s_filter.band_peak[b] = signal;
            } else {
                s_filter.band_peak[b] *= 0.992f;
                if (s_filter.band_peak[b] < peak_min) s_filter.band_peak[b] = peak_min;
            }

            float val = (s_filter.band_peak[b] > 1e-8f) ? (signal / s_filter.band_peak[b]) : 0.0f;
            if (val > 1.0f) val = 1.0f;

            /* 上升快（0.6）/ 下降按用户 smooth 参数调，让峰值跟手、尾音绵软 */
            float alpha              = (val > s_filter.smooth_bands[b]) ? 0.6f : (local_smooth / 255.0f * 0.3f + 0.05f);
            s_filter.smooth_bands[b] = s_filter.smooth_bands[b] * (1.0f - alpha) + val * alpha;
            current_bands[b]         = s_filter.smooth_bands[b];
        }

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

        /* 门限以下整体清零,避免静默环境下底噪扰动灯效 */
        float squelch_f  = (float)s_squelch / 255.0f * 0.15f;
        float out_volume = (s_filter.smooth_vol > squelch_f) ? s_filter.smooth_vol : 0;
        if (out_volume == 0) {
            for (int b = 0; b < MIC_BANDS; b++) current_bands[b] = 0;
        }

        float beat = beat_detect(raw_energy);
        if (out_volume == 0) beat = 0;

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
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    mic_apply_settings(st);

    generate_blackman_harris_window(s_wind, MIC_FFT_SIZE);
    dsps_fft2r_init_fc32(NULL, MIC_FFT_SIZE);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear        = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s channel create failed: %s", esp_err_to_name(err));
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
        ESP_LOGE(TAG, "i2s channel init failed");
        return;
    }

    /* mic_task 绑 core 1,让 core 0 专注渲染避免抢 RMT 时序 */
    xTaskCreatePinnedToCore(mic_task, "mic", 8192, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "mic ready, sck: %d, ws: %d, din: %d", st->mic_sck, st->mic_ws, st->mic_din);
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