/**
 * @file audio_utils.h
 * @brief 音频处理工具函数声明
 */

#ifndef AUDIO_UTILS_H
#define AUDIO_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include "audio_config.h"

/**
 * @brief 初始化高通滤波器
 * @param cutoff_hz 截止频率
 * @param sample_rate 采样率
 */
void hpf_init(float cutoff_hz, float sample_rate);

/**
 * @brief 高通滤波器处理单个样本
 * @param sample 输入样本
 * @return 滤波后的样本
 */
float hpf_filter(float sample);

/**
 * @brief 计算音频统计信息
 * @param buffer 音频缓冲区
 * @param samples 样本数
 * @param stats 输出统计信息
 */
void audio_calculate_stats(const int16_t *buffer, uint32_t samples, audio_stats_t *stats);

/**
 * @brief 音频归一化处理
 * @param buffer 音频缓冲区（原地处理）
 * @param samples 样本数
 * @param target_level 目标水平(0.0-1.0)
 */
void audio_normalize(int16_t *buffer, uint32_t samples, float target_level);

/**
 * @brief 将int16音频转换为float格式
 * @param int_buffer 输入int16缓冲区
 * @param float_buffer 输出float缓冲区
 * @param samples 样本数
 */
void audio_int16_to_float(const int16_t *int_buffer, float *float_buffer, uint32_t samples);

/**
 * @brief 检测语音活动
 * @param buffer 音频缓冲区
 * @param samples 样本数
 * @param threshold_db 检测阈值(dB)
 * @return 如果检测到语音返回true
 */
bool audio_has_voice_activity(const int16_t *buffer, uint32_t samples, float threshold_db);

/**
 * @brief 分割音频为帧
 * @param buffer 音频缓冲区
 * @param buffer_samples 缓冲区样本数
 * @param frame_size 每帧样本数
 * @param frames_out 输出帧指针数组
 * @param max_frames 最大帧数
 * @return 实际帧数
 */
uint32_t audio_split_frames(const int16_t *buffer, uint32_t buffer_samples,
                           uint32_t frame_size, int16_t **frames_out,
                           uint32_t max_frames);

#endif // AUDIO_UTILS_H
