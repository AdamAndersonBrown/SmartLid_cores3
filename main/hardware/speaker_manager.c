#include "speaker_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t audio_semaphore = NULL;
static const char *TAG = "SPEAKER";

void core2_set_amp(bool enable) {
    uint8_t cmd[2] = {0x94, enable ? 0x04 : 0x00};
    i2c_master_write_to_device(I2C_NUM_0, 0x34, cmd, 2, pdMS_TO_TICKS(10));
}

static void speaker_task(void *pvParameters) {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 160
    };
    i2s_pin_config_t pin_config = { .bck_io_num = 34, .ws_io_num = 33, .data_out_num = 13, .data_in_num = I2S_PIN_NO_CHANGE };
    
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);
    ESP_LOGI(TAG, "I2S Audio Amplifier Initialized (Muted).");

    int16_t sample_buffer[228 * 2];
    size_t bytes_written;

    while(1) {
        if (xSemaphoreTake(audio_semaphore, portMAX_DELAY) == pdTRUE) {
            core2_set_amp(true); // POWER ON AMP
            vTaskDelay(pdMS_TO_TICKS(20)); // Wait for AXP192 voltage to stabilize

            ESP_LOGW(TAG, "HISSSSSS...");
            for (int loop = 0; loop < 140; loop++) {
                for (int i = 0; i < 114; i++) {
                    int16_t noise = (esp_random() % 40000) - 20000;
                    sample_buffer[i * 2] = noise; sample_buffer[i * 2 + 1] = noise;
                }
                for (int i = 114; i < 228; i++) {
                    sample_buffer[i * 2] = 0; sample_buffer[i * 2 + 1] = 0;
                }
                i2s_write(I2S_NUM_0, sample_buffer, sizeof(sample_buffer), &bytes_written, portMAX_DELAY);
            }
            i2s_zero_dma_buffer(I2S_NUM_0);
            core2_set_amp(false); // POWER OFF AMP
        }
    }
}

void speaker_manager_init(void) {
    audio_semaphore = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(speaker_task, "speaker_task", 4096, NULL, 2, NULL, 1);
}
void speaker_play_rattle(void) {
    if (audio_semaphore != NULL) xSemaphoreGive(audio_semaphore);
}
