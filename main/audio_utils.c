/**
 * @file audio_utils.c
 * @brief 音频处理工具函数
 */

#include <math.h>
#include "audio_utils.h"

// ============= 高通滤波器实现 (一阶IIR) =============
typedef struct {
    float prev_input;
    float prev_output;
    float alpha;  // 滤波器系数
} hpf_state_t;

static hpf_state_t hpf_state = {0};

// 计算高通滤波器系数
static float calculate_hpf_alpha(float cutoff_hz, float sample_rate)
{
    float rc = 1.0f / (2.0f * M_PI * cutoff_hz);
    float dt = 1.0f / sample_rate;
    return rc / (rc + dt);
}

void hpf_init(float cutoff_hz, float sample_rate)
{
    hpf_state.alpha = calculate_hpf_alpha(cutoff_hz, sample_rate);
    hpf_state.prev_input = 0.0f;
    hpf_state.prev_output = 0.0f;
}

float hpf_filter(float sample)
{
    float output = hpf_state.alpha * (hpf_state.prev_output + sample - hpf_state.prev_input);
    hpf_state.prev_input = sample;
    hpf_state.prev_output = output;
    return output;
}

// ============= 音频统计计算 =============
void audio_calculate_stats(const int16_t *buffer, uint32_t samples, audio_stats_t *stats)
{
    if (!buffer || !stats || samples == 0) {
        if (stats) {
            stats->sample_count = 0;
            stats->has_valid_signal = false;
        }
        return;
    }

    float rms_sum = 0.0f;
    int16_t peak = 0;
    float energy = 0.0f;

    // 遍历样本计算统计数据
    for (uint32_t i = 0; i < samples; i++) {
        int16_t sample = buffer[i];
        float normalized = (float)sample / 32768.0f;

        // RMS计算
        rms_sum += normalized * normalized;

        // 峰值
        if (sample < 0) {
            peak = ((-sample) > peak) ? (-sample) : peak;
        } else {
            peak = (sample > peak) ? sample : peak;
        }

        // 能量
        energy += normalized * normalized;
    }

    stats->rms_level = sqrtf(rms_sum / samples);
    stats->peak_level = (float)peak / 32768.0f;
    stats->signal_energy = energy / samples;
    stats->sample_count = samples;

    // 判断是否有有效信号（RMS > -40dB）
    stats->has_valid_signal = (stats->rms_level > 0.01f);
}

// ============= 音频归一化 =============
void audio_normalize(int16_t *buffer, uint32_t samples, float target_level)
{
    if (!buffer || samples == 0) return;

    // 找到最大绝对值
    int16_t max_abs = 0;
    for (uint32_t i = 0; i < samples; i++) {
        int16_t abs_val = buffer[i] < 0 ? -buffer[i] : buffer[i];
        if (abs_val > max_abs) {
            max_abs = abs_val;
        }
    }

    if (max_abs == 0) return;

    // 计算缩放因子
    float scale = (target_level * 32768.0f) / max_abs;

    // 应用缩放
    for (uint32_t i = 0; i < samples; i++) {
        int32_t scaled = (int32_t)(buffer[i] * scale);
        // 防止溢出
        if (scaled > 32767) {
            buffer[i] = 32767;
        } else if (scaled < -32768) {
            buffer[i] = -32768;
        } else {
            buffer[i] = (int16_t)scaled;
        }
    }
}

// ============= 转换为浮点数组 =============
void audio_int16_to_float(const int16_t *int_buffer, float *float_buffer, uint32_t samples)
{
    if (!int_buffer || !float_buffer) return;

    for (uint32_t i = 0; i < samples; i++) {
        float_buffer[i] = (float)int_buffer[i] / 32768.0f;
    }
}

// ============= 简单的低频噪声检测 =============
bool audio_has_voice_activity(const int16_t *buffer, uint32_t samples, float threshold_db)
{
    audio_stats_t stats;
    audio_calculate_stats(buffer, samples, &stats);

    // 转换为dB (-40dB为静音阈值)
    float db = 20.0f * log10f(stats.rms_level + 1e-6f);
    
    return db > threshold_db;
}

// ============= 音频帧分割 =============
uint32_t audio_split_frames(const int16_t *buffer, uint32_t buffer_samples,
                           uint32_t frame_size, int16_t **frames_out,
                           uint32_t max_frames)
{
    if (!buffer || !frames_out || frame_size == 0) return 0;

    uint32_t frame_count = 0;
    uint32_t offset = 0;

    while (offset + frame_size <= buffer_samples && frame_count < max_frames) {
        frames_out[frame_count] = (int16_t *)&buffer[offset];
        offset += frame_size;
        frame_count++;
    }

    return frame_count;
}
