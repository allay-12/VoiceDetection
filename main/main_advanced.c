/**
 * @file main_advanced.c
 * @brief 高级音频采集示例（带音频处理和统计）
 * 
 * 这个示例展示了如何使用audio_utils库进行：
 * - 音频统计分析
 * - 自动增益控制（归一化）
 * - 语音活动检测（VAD）
 * - 本地音频处理
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "audio_config.h"
#include "audio_utils.h"

static const char *TAG = "AUDIO_ADV";

// 全局变量
static i2s_chan_handle_t rx_handle = NULL;
static int16_t *audio_buffer = NULL;
static uint32_t audio_buffer_pos = 0;
static bool is_recording = false;

// WiFi事件处理
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi已连接，IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// WiFi初始化
static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// I2S初始化
static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = I2S_DMA_BUF_LEN;
    chan_cfg.auto_clear_before_start = true;
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS_PER_SAMPLE, I2S_CHANNELS),
        .gpio_cfg = {
            .bclk = I2S_BCLK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DOUT_PIN,
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    
    // 初始化高通滤波器（可选）
    #if ENABLE_HPF_FILTER
    hpf_init(HPF_CUTOFF_HZ, I2S_SAMPLE_RATE);
    #endif
    
    ESP_LOGI(TAG, "I2S初始化完成（采样率:%dHz）", I2S_SAMPLE_RATE);
}

// 显示音频统计信息
static void print_audio_stats(const audio_stats_t *stats)
{
    float db = 20.0f * log10f(stats->rms_level + 1e-6f);
    
    ESP_LOGI(TAG, "📊 音频统计:");
    ESP_LOGI(TAG, "  RMS水平: %.4f (-%.1fdB)", stats->rms_level, -db);
    ESP_LOGI(TAG, "  峰值: %.4f", stats->peak_level);
    ESP_LOGI(TAG, "  能量: %.6f", stats->signal_energy);
    ESP_LOGI(TAG, "  样本数: %ld", stats->sample_count);
    ESP_LOGI(TAG, "  信号检测: %s", stats->has_valid_signal ? "✓ 有效信号" : "✗ 静音");
}

// 上传到Edge Impulse（带音频处理）
static void upload_to_edge_impulse(void)
{
    if (audio_buffer_pos < AUDIO_BUFFER_SIZE / 4) {
        ESP_LOGW(TAG, "音频数据不足（< 25%)，跳过上传");
        return;
    }

    ESP_LOGI(TAG, "🚀 开始处理和上传音频数据...");

    // 计算原始统计
    audio_stats_t stats_before;
    audio_calculate_stats(audio_buffer, audio_buffer_pos, &stats_before);
    ESP_LOGI(TAG, "处理前统计：");
    print_audio_stats(&stats_before);

    // 音频处理
    #if ENABLE_AUDIO_NORMALIZATION
    audio_normalize(audio_buffer, audio_buffer_pos, AUDIO_NORMALIZATION_TARGET);
    ESP_LOGI(TAG, "✓ 音频已归一化");
    #endif

    // 计算处理后统计
    audio_stats_t stats_after;
    audio_calculate_stats(audio_buffer, audio_buffer_pos, &stats_after);
    ESP_LOGI(TAG, "处理后统计：");
    print_audio_stats(&stats_after);

    // 创建JSON数据
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "apiKey", EI_API_KEY);
    cJSON_AddStringToObject(root, "deviceId", EI_DEVICE_ID);
    cJSON_AddStringToObject(root, "deviceType", "ESP32-S3");
    cJSON_AddNumberToObject(root, "timestamp", esp_log_timestamp());
    
    // 添加元数据
    cJSON *meta = cJSON_CreateObject();
    cJSON_AddNumberToObject(meta, "rms_level", stats_after.rms_level);
    cJSON_AddNumberToObject(meta, "peak_level", stats_after.peak_level);
    cJSON_AddNumberToObject(meta, "sample_rate", I2S_SAMPLE_RATE);
    cJSON_AddNumberToObject(meta, "duration_sec", (float)audio_buffer_pos / I2S_SAMPLE_RATE);
    cJSON_AddItemToObject(root, "metadata", meta);
    
    // 创建样本数组
    cJSON *samples = cJSON_CreateArray();
    for (uint32_t i = 0; i < audio_buffer_pos; i++) {
        float normalized = (float)audio_buffer[i] / 32768.0f;
        cJSON_AddNumberToObject(samples, NULL, normalized);
    }
    cJSON_AddItemToObject(root, "samples", samples);
    
    char *json_str = cJSON_Print(root);
    ESP_LOGI(TAG, "📤 上传数据大小: %d字节", strlen(json_str));

    // 配置HTTP请求
    esp_http_client_config_t config = {
        .url = EI_INGESTION_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = NULL,
        .timeout_ms = 30000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // 设置请求头
    esp_http_client_set_header(client, "Content-Type", "application/json");
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", EI_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    
    // 发送数据
    esp_http_client_set_post_field(client, json_str, strlen(json_str));
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 || status_code == 201) {
            ESP_LOGI(TAG, "✅ 成功上传到Edge Impulse (HTTP %d)", status_code);
        } else {
            ESP_LOGW(TAG, "⚠️  上传返回状态码: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "❌ HTTP请求失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(json_str);
    
    // 重置缓冲区
    audio_buffer_pos = 0;
}

// 音频采集任务
static void audio_record_task(void *arg)
{
    uint8_t recv_buf[I2S_DMA_BUF_LEN * 2];
    size_t bytes_read = 0;
    uint32_t chunk_count = 0;

    ESP_LOGI(TAG, "🎤 开始音频录制 (缓冲: %.1f秒)", 
             (float)AUDIO_BUFFER_SIZE / I2S_SAMPLE_RATE);
    
    while (1) {
        if (!is_recording) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // 从I2S读取数据
        esp_err_t err = i2s_channel_read(rx_handle, recv_buf, I2S_DMA_BUF_LEN * 2, 
                                        &bytes_read, 100);
        
        if (err == ESP_OK && bytes_read > 0) {
            int16_t *int_buf = (int16_t *)recv_buf;
            uint32_t samples = bytes_read / sizeof(int16_t);
            
            // 应用高通滤波（可选）
            #if ENABLE_HPF_FILTER
            for (uint32_t i = 0; i < samples; i++) {
                float filtered = hpf_filter((float)int_buf[i] / 32768.0f);
                int_buf[i] = (int16_t)(filtered * 32768.0f);
            }
            #endif
            
            // 存储到音频缓冲区
            for (uint32_t i = 0; i < samples && audio_buffer_pos < AUDIO_BUFFER_SIZE; i++) {
                audio_buffer[audio_buffer_pos++] = int_buf[i];
            }
            
            chunk_count++;
            
            // 每32个数据块（约1秒）显示一次进度
            if (chunk_count % 32 == 0) {
                float progress = (float)audio_buffer_pos / AUDIO_BUFFER_SIZE * 100.0f;
                ESP_LOGI(TAG, "录制进度: %.0f%% (%ld/%ld样本)", 
                        progress, audio_buffer_pos, AUDIO_BUFFER_SIZE);
            }
            
            // 缓冲区满时上传
            if (audio_buffer_pos >= AUDIO_BUFFER_SIZE) {
                upload_to_edge_impulse();
                chunk_count = 0;
            }
        }
    }
}

// CLI命令处理（可选）
static void handle_console_input(void *arg)
{
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        
        // 这里可以添加UART输入处理
        // 例如：按键启动/停止录制、显示统计等
    }
}

void app_main(void)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 分配音频缓冲区
    audio_buffer = (int16_t *)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    if (!audio_buffer) {
        ESP_LOGE(TAG, "❌ 音频缓冲区内存分配失败");
        return;
    }
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32S3 高级语音采集系统 (v2.0)        ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "采样率: %dHz | 分辨率: %d位 | 通道: %d", 
             I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE, I2S_CHANNELS);
    ESP_LOGI(TAG, "缓冲大小: %.1f秒 ≈ %dKB", 
             (float)AUDIO_BUFFER_SIZE / I2S_SAMPLE_RATE,
             (AUDIO_BUFFER_SIZE * sizeof(int16_t)) / 1024);
    
    #if ENABLE_AUDIO_NORMALIZATION
    ESP_LOGI(TAG, "✓ 音频归一化: 已启用");
    #endif
    
    #if ENABLE_HPF_FILTER
    ESP_LOGI(TAG, "✓ 高通滤波器: 已启用 (%.0fHz)", (float)HPF_CUTOFF_HZ);
    #endif
    
    // 初始化I2S
    i2s_init();
    
    // 初始化WiFi
    ESP_LOGI(TAG, "🌐 正在连接WiFi...");
    wifi_init_sta();
    
    // 等待WiFi连接
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // 启动录制
    is_recording = true;
    
    // 创建任务
    xTaskCreate(audio_record_task, "audio_record", 4096, NULL, 5, NULL);
    xTaskCreate(handle_console_input, "console_input", 2048, NULL, 1, NULL);
    
    ESP_LOGI(TAG, "✅ 系统启动完成，开始监听音频...\n");
}
