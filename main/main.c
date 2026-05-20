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
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "AUDIO_COLLECT";

// I2S和引脚配置
#define I2S_NUM I2S_NUM_0
#define I2S_SAMPLE_RATE 16000  // 16kHz采样率
#define I2S_CHANNELS 1          // 单声道
#define I2S_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_32BIT  // 32位
#define I2S_DMA_BUF_COUNT 4
#define I2S_DMA_BUF_LEN 1024

// INMP441引脚配置
#define I2S_BCLK_PIN 8    // SCK
#define I2S_WS_PIN 3      // LRCLK/WS
#define I2S_DOUT_PIN 18   // SD/DIN

// 音频缓冲配置
#define AUDIO_BUFFER_SIZE (I2S_SAMPLE_RATE * 4)  // 4秒音频缓冲
#define AUDIO_CHUNK_SIZE 1024                      // 每次采样大小

// WiFi配置 - 需要用户修改为自己的WiFi
#define WIFI_SSID "your_WIFI_name"
#define WIFI_PASSWORD "your_WIFI_password"

// Edge Impulse配置
#define EI_API_KEY "your_EdgeImpulse_API_KEY"
#define EI_DEVICE_ID "esp32s3_voice"
#define EI_PROJECT_NAME "voice_detection"

static i2s_chan_handle_t rx_handle = NULL;
static int16_t *audio_buffer = NULL;
static uint32_t audio_buffer_pos = 0;

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
        ESP_LOGI(TAG, "WiFi已连接，IP地址: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// 初始化WiFi
static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
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

// 初始化I2S用于INMP441
static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = I2S_DMA_BUF_LEN;
    chan_cfg.auto_clear = true;
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_MONO
    );
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .bclk = I2S_BCLK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DOUT_PIN,
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    
    ESP_LOGI(TAG, "I2S初始化完成");
}

// 上传音频数据到Edge Impulse
static void upload_to_edge_impulse(void)
{
    if (audio_buffer_pos < AUDIO_BUFFER_SIZE / 2) {
        ESP_LOGW(TAG, "音频数据不足，跳过上传");
        return;
    }

    uint32_t pcm_data_size = audio_buffer_pos * sizeof(int16_t);
    uint32_t file_size = pcm_data_size + 36;
    uint32_t byte_rate = I2S_SAMPLE_RATE * 2; // 16-bit mono

    // 1. 构建标准的WAV文件头 (44字节)
    uint8_t wav_header[44] = {
        'R', 'I', 'F', 'F',
        file_size & 0xff, (file_size >> 8) & 0xff, (file_size >> 16) & 0xff, (file_size >> 24) & 0xff,
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0,          
        1, 0,                 
        1, 0,                 
        I2S_SAMPLE_RATE & 0xff, (I2S_SAMPLE_RATE >> 8) & 0xff, (I2S_SAMPLE_RATE >> 16) & 0xff, (I2S_SAMPLE_RATE >> 24) & 0xff,
        byte_rate & 0xff, (byte_rate >> 8) & 0xff, (byte_rate >> 16) & 0xff, (byte_rate >> 24) & 0xff,
        2, 0,                 
        16, 0,                
        'd', 'a', 't', 'a',
        pcm_data_size & 0xff, (pcm_data_size >> 8) & 0xff, (pcm_data_size >> 16) & 0xff, (pcm_data_size >> 24) & 0xff
    };

    // 2. 构建 multipart/form-data 的数据边界 (把WAV文件包在表单里)
    const char *boundary = "EdgeImpulseBoundary";
    char pre_boundary[256];
    snprintf(pre_boundary, sizeof(pre_boundary),
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"data\"; filename=\"jiuming.wav\"\r\n"
             "Content-Type: audio/wav\r\n\r\n", boundary);
    
    char post_boundary[64];
    snprintf(post_boundary, sizeof(post_boundary), "\r\n--%s--\r\n", boundary);

    // 计算总的 HTTP 载荷长度
    uint32_t total_content_length = strlen(pre_boundary) + sizeof(wav_header) + pcm_data_size + strlen(post_boundary);

    // 3. 配置HTTP客户端 -> 🚨 关键修改：网址从 data 改为了 files ！！！
    esp_http_client_config_t config = {
        .url = "https://ingestion.edgeimpulse.com/api/training/files", 
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // 4. 设置请求头 -> 🚨 关键修改：告诉服务器我们发的是带边界的表单数据
    esp_http_client_set_header(client, "x-api-key", EI_API_KEY);
    esp_http_client_set_header(client, "x-label", "your_voice_label");  // ⬅️ 这里改成你想录的标签，比如 zhaohuole
    
    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_header(client, "Connection", "close");

    ESP_LOGI(TAG, "开始打包上传 %d 字节的表单数据...", (int)total_content_length);

    // 5. 按顺序分段发送数据 (像寄快递一样连贯发过去，单片机内存完全无压力)
    esp_err_t err = esp_http_client_open(client, total_content_length);
    if (err == ESP_OK) {
        esp_http_client_write(client, pre_boundary, strlen(pre_boundary));           // 发送开头边界
        esp_http_client_write(client, (const char*)wav_header, sizeof(wav_header));  // 发送WAV头
        esp_http_client_write(client, (const char*)audio_buffer, pcm_data_size);     // 发送音频数据体
        esp_http_client_write(client, post_boundary, strlen(post_boundary));         // 发送结尾边界
        
        esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        
        if (status_code == 200) {
            ESP_LOGI(TAG, "✅ 成功上传到Edge Impulse！快去网页端 Data acquisition 看看吧");
        } else {
            ESP_LOGE(TAG, "❌ 上传失败，状态码: %d", status_code);
            char response_buf[256] = {0};
            int read_len = esp_http_client_read_response(client, response_buf, sizeof(response_buf) - 1);
            if (read_len > 0) {
                ESP_LOGE(TAG, "⚠️ 退回原因: %s", response_buf);
            }
        }
    } else {
        ESP_LOGE(TAG, "打开HTTP连接失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    
    // 重置缓冲区，准备录制下一句
    audio_buffer_pos = 0;
    vTaskDelay(2000 / portTICK_PERIOD_MS);
}

static float filter_last_sample = 0.0f;
static float filter_last_out = 0.0f;
// 音频采集任务
static void audio_record_task(void *arg)
{
    int32_t recv_buf[I2S_DMA_BUF_LEN]; 
    size_t bytes_read = 0;

    ESP_LOGI(TAG, "开始音频录制...");
    
    while (1) {
        esp_err_t err = i2s_channel_read(rx_handle, recv_buf, sizeof(recv_buf), &bytes_read, 100);
        
        if (err == ESP_OK && bytes_read > 0) {
            uint32_t samples = bytes_read / sizeof(int32_t);
            
            for (uint32_t i = 0; i < samples && audio_buffer_pos < AUDIO_BUFFER_SIZE; i++) {
                
                // 👇 3. 把 for 循环里的内容完全替换成你的这段代码：
                int16_t sample = recv_buf[i] >> 16;
                
                float x = (float)sample;
                float y = x - filter_last_sample + 0.995f * filter_last_out;
                
                filter_last_sample = x;
                filter_last_out = y;
                
                int32_t amplified = (int32_t)(y * 1.0f);
                
                if (amplified > 32767) amplified = 32767;
                if (amplified < -32768) amplified = -32768;
                
                audio_buffer[audio_buffer_pos++] = (int16_t)amplified;
                // 👆 替换结束
            }
            
            if (audio_buffer_pos >= AUDIO_BUFFER_SIZE) {
                ESP_LOGI(TAG, "音频缓冲区已满，开始上传...");
                upload_to_edge_impulse();
            }
        }
    }
}

void app_main(void)
{
    // 初始化NVS（用于WiFi配置存储）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 分配音频缓冲区
    audio_buffer = (int16_t *)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    if (!audio_buffer) {
        ESP_LOGE(TAG, "音频缓冲区内存分配失败");
        return;
    }
    
    ESP_LOGI(TAG, "==== ESP32S3 语音采集系统启动 ====");
    ESP_LOGI(TAG, "采样率: %dHz, 分辨率: %d位", I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE);
    
    // 初始化I2S
    i2s_init();
    
    // 初始化WiFi
    ESP_LOGI(TAG, "正在连接WiFi...");
    wifi_init_sta();
    
    // 等待WiFi连接
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // 创建音频采集任务
    xTaskCreate(audio_record_task, "audio_record_task", 8192, NULL, 5, NULL);
}