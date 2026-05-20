/**
 * @file audio_config.h
 * @brief 音频采集和处理配置
 */

#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============= I2S配置 =============
#define I2S_NUM I2S_NUM_0
#define I2S_SAMPLE_RATE 16000      // 采样率: 16kHz (语音识别标准)
#define I2S_CHANNELS 1              // 单声道
#define I2S_BITS_PER_SAMPLE 16      // 16位
#define I2S_DMA_BUF_COUNT 4
#define I2S_DMA_BUF_LEN 1024

// ============= INMP441引脚配置 =============
#define I2S_BCLK_PIN 8      // SCK
#define I2S_WS_PIN 3        // LRCLK
#define I2S_DOUT_PIN 18     // SD/DIN

// ============= 音频缓冲配置 =============
#define AUDIO_BUFFER_SECONDS 10                        // 缓冲持续时间（秒）
#define AUDIO_BUFFER_SIZE (I2S_SAMPLE_RATE * AUDIO_BUFFER_SECONDS)
#define AUDIO_CHUNK_SIZE 1024

// ============= WiFi配置 =============
#define WIFI_SSID "your_ssid"
#define WIFI_PASSWORD "your_password"
#define WIFI_CONNECT_TIMEOUT_MS 10000

// ============= Edge Impulse配置 =============
#define EI_API_KEY "your_ei_api_key"
#define EI_DEVICE_ID "esp32s3_voice"
#define EI_PROJECT_NAME "voice_detection"
#define EI_INGESTION_URL "https://ingestion.edgeimpulse.com/api/training/data"

// ============= 音频处理选项 =============
#define ENABLE_AUDIO_NORMALIZATION 1    // 启用音频归一化
#define ENABLE_HPF_FILTER 1              // 启用高通滤波器（去低频噪声）
#define ENABLE_LOCAL_STORAGE 0           // 启用本地存储（需要SPIFFS）

// ============= 高通滤波器参数 =============
#define HPF_CUTOFF_HZ 100                // 高通滤波截止频率
#define HPF_SAMPLE_RATE I2S_SAMPLE_RATE

// ============= 音频指标 =============
#define AUDIO_MAX_AMPLITUDE 32768.0      // 16位最大值
#define AUDIO_NORMALIZATION_TARGET 0.8   // 目标归一化水平

/**
 * @brief 音频统计信息
 */
typedef struct {
    float rms_level;          // RMS声压级
    float peak_level;         // 峰值
    float signal_energy;      // 信号能量
    uint32_t sample_count;    // 样本数
    bool has_valid_signal;    // 是否有有效信号
} audio_stats_t;

/**
 * @brief 初始化音频采集系统
 * @return 成功返回true，失败返回false
 */
bool audio_system_init(void);

/**
 * @brief 获取音频统计信息
 * @param stats 统计信息指针
 */
void audio_get_stats(audio_stats_t *stats);

/**
 * @brief 启用/禁用音频采集
 */
void audio_start_recording(void);
void audio_stop_recording(void);

/**
 * @brief 高通滤波器（防止低频噪声）
 */
float hpf_filter(float sample);

#endif // AUDIO_CONFIG_H
