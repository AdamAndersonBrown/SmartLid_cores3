#include "common_defs.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "imu_telemetry_task.h"

#define BMI270_ADDR 0x69 
#define I2C_MASTER_NUM I2C_NUM_0

static const char *TAG = "IMU";
extern QueueHandle_t imu_queue;

RingbufHandle_t telemetry_rb;

extern void display_manager_wake(void);
extern void udp_batch_task(void *pvParameters);

static void imu_sensor_task(void *pvParameters) {
    ESP_LOGI(TAG, "BMI270 Sensor Task booted on Core %d", xPortGetCoreID());

    if (i2c_mutex && xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
        uint8_t init_cmds[][2] = {
            {0x7C, 0x00}, {0x7D, 0x0E}, {0x40, 0xA8}, 
            {0x42, 0xA9}, {0x41, 0x00}, {0x43, 0x03}
        };
        for(int i = 0; i < 6; i++) {
            i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, init_cmds[i], 2, pdMS_TO_TICKS(100));
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        xSemaphoreGive(i2c_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200)); 

    uint8_t raw_data[12];
    int16_t last_ax = 0, last_ay = 0, last_az = 0;

    while (1) {
        uint8_t reg = 0x0C; 
        if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (i2c_master_write_read_device(I2C_MASTER_NUM, BMI270_ADDR, &reg, 1, raw_data, 12, pdMS_TO_TICKS(10)) == ESP_OK) {
                // Little-Endian Shift
                int16_t acc_x = (int16_t)((raw_data[1] << 8) | raw_data[0]);
                int16_t acc_y = (int16_t)((raw_data[3] << 8) | raw_data[2]);
                int16_t acc_z = (int16_t)((raw_data[5] << 8) | raw_data[4]);
                int16_t gyro_x = (int16_t)((raw_data[7] << 8) | raw_data[6]);
                int16_t gyro_y = (int16_t)((raw_data[9] << 8) | raw_data[8]);
                int16_t gyro_z = (int16_t)((raw_data[11] << 8) | raw_data[10]);

                int16_t delta = abs(acc_x - last_ax) + abs(acc_y - last_ay) + abs(acc_z - last_az);
                
                // Assuming imu_queue is defined elsewhere in your system
                imu_sample_t sample = {acc_x, acc_y, acc_z, gyro_x, gyro_y, gyro_z};
                xQueueSend(imu_queue, &sample, 0); 
                
                if (delta > 6000) display_manager_wake(); 
                last_ax = acc_x; last_ay = acc_y; last_az = acc_z;
            }
            xSemaphoreGive(i2c_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 50Hz rolling window
    }
}

esp_err_t start_imu_telemetry_task(void) {
    ESP_LOGI(TAG, "Starting BMI270 AI Telemetry Pipeline...");
    telemetry_rb = xRingbufferCreate(10240, RINGBUF_TYPE_NOSPLIT);
    xTaskCreatePinnedToCore(imu_sensor_task, "imu_task", 4096, NULL, 5, NULL, 0);
    // xTaskCreatePinnedToCore(udp_batch_task, "udp_task", 4096, NULL, 2, NULL, 0);
    return ESP_OK;
}
