#include "common_defs.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "imu_telemetry_task.h"
#include "../core/bmi270_context.h"
#include <string.h>

#define BMI270_ADDR 0x69 
#define I2C_MASTER_NUM I2C_NUM_0

static const char *TAG = "IMU";
extern QueueHandle_t imu_queue;

RingbufHandle_t telemetry_rb;

extern void display_manager_wake(void);

static void imu_sensor_task(void *pvParameters) {
    ESP_LOGI(TAG, "BMI270 Sensor Task booted on Core %d", xPortGetCoreID());

    if (i2c_mutex && xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
        uint8_t buf[2];
        
        buf[0] = 0x7E; buf[1] = 0xB6; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(50));

        buf[0] = 0x6B; buf[1] = 0x20; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100));
        buf[0] = 0x7D; buf[1] = 0x0F; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100));
        buf[0] = 0x7C; buf[1] = 0x00; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(10));

        buf[0] = 0x59; buf[1] = 0x00; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100));
        
        uint8_t *temp_data = (uint8_t *)malloc(bmi270_context_config_file_size + 1);
        if (temp_data) {
            temp_data[0] = 0x5E;
            memcpy(&temp_data[1], bmi270_context_config_file, bmi270_context_config_file_size);
            i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, temp_data, bmi270_context_config_file_size + 1, pdMS_TO_TICKS(1000));
            free(temp_data);
        }

        buf[0] = 0x59; buf[1] = 0x01; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(150)); 

        // 4. ML BASELINE CONFIGURATION
        buf[0] = 0x7D; buf[1] = 0x0E; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100)); // Accel/Gyro On
        buf[0] = 0x40; buf[1] = 0xA8; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100)); // ODR 100Hz
        buf[0] = 0x42; buf[1] = 0xA8; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100)); // ODR 100Hz
        buf[0] = 0x7C; buf[1] = 0x00; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100)); // Continuous Power
        buf[0] = 0x41; buf[1] = 0x00; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100)); // +-2g
        buf[0] = 0x43; buf[1] = 0x03; i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, buf, 2, pdMS_TO_TICKS(100)); // +-250dps

        xSemaphoreGive(i2c_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200)); 

    uint8_t raw_data[12];
    int16_t last_ax = 0, last_ay = 0, last_az = 0;

    while (1) {
        uint8_t reg = 0x0C; 
        if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (i2c_master_write_read_device(I2C_MASTER_NUM, BMI270_ADDR, &reg, 1, raw_data, 12, pdMS_TO_TICKS(10)) == ESP_OK) {
                int16_t raw_x = (int16_t)((raw_data[1] << 8) | raw_data[0]);
                int16_t raw_y = (int16_t)((raw_data[3] << 8) | raw_data[2]);
                int16_t raw_z = (int16_t)((raw_data[5] << 8) | raw_data[4]);
                
                int16_t raw_gx = (int16_t)((raw_data[7] << 8) | raw_data[6]);
                int16_t raw_gy = (int16_t)((raw_data[9] << 8) | raw_data[8]);
                int16_t raw_gz = (int16_t)((raw_data[11] << 8) | raw_data[10]);

                // PERFECT CORE2 ALIGNMENT (180-deg flip around Y-axis)
                int16_t acc_x = -raw_x;
                int16_t acc_y = raw_y;
                int16_t acc_z = -raw_z;
                
                int16_t gyro_x = -raw_gx;
                int16_t gyro_y = raw_gy;
                int16_t gyro_z = -raw_gz;

                int16_t delta = abs(acc_x - last_ax) + abs(acc_y - last_ay) + abs(acc_z - last_az);
                
                // ARCHITECT FIX: IMU Low Power State Machine
                static bool imu_is_awake = false;
                static int awake_frame_count = 0;
                
                if (!imu_is_awake && delta > 6000) {
                    uint8_t wake_cmds[][2] = {{0x7C, 0x00}, {0x7D, 0x0E}}; // APS OFF, Gyro ON
                    i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, wake_cmds[0], 2, pdMS_TO_TICKS(10));
                    i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, wake_cmds[1], 2, pdMS_TO_TICKS(10));
                    imu_is_awake = true;
                    awake_frame_count = 150; // Keep awake for 3 seconds (50Hz)
                    display_manager_wake();
                    ESP_LOGW(TAG, "Kinetic event! Waking IMU Gyroscope for 3 seconds.");
                }
                
                if (imu_is_awake) {
                    awake_frame_count--;
                    if (awake_frame_count <= 0) {
                        uint8_t sleep_cmds[][2] = {{0x7D, 0x04}, {0x7C, 0x02}}; // Gyro OFF, APS ON
                        i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, sleep_cmds[0], 2, pdMS_TO_TICKS(10));
                        i2c_master_write_to_device(I2C_MASTER_NUM, BMI270_ADDR, sleep_cmds[1], 2, pdMS_TO_TICKS(10));
                        imu_is_awake = false;
                        ESP_LOGI(TAG, "Timeout reached. IMU returning to super low power mode.");
                    }
                }

                // Push zeros for Gyro if asleep to stabilize ML filter
                imu_sample_t sample = {
                    acc_x, acc_y, acc_z, 
                    imu_is_awake ? gyro_x : 0, 
                    imu_is_awake ? gyro_y : 0, 
                    imu_is_awake ? gyro_z : 0
                };
                xQueueSend(imu_queue, &sample, 0);
                ESP_LOGI(TAG, "RAW IMU -> Accel: [%6d, %6d, %6d] | Gyro: [%6d, %6d, %6d]", acc_x, acc_y, acc_z, imu_is_awake ? gyro_x : 0, imu_is_awake ? gyro_y : 0, imu_is_awake ? gyro_z : 0); 
                
                last_ax = acc_x; last_ay = acc_y; last_az = acc_z;
            }
            xSemaphoreGive(i2c_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t start_imu_telemetry_task(void) {
    ESP_LOGI(TAG, "Starting BMI270 AI Telemetry Pipeline...");
    telemetry_rb = xRingbufferCreate(10240, RINGBUF_TYPE_NOSPLIT);
    xTaskCreatePinnedToCore(imu_sensor_task, "imu_task", 4096, NULL, 5, NULL, 0);
    return ESP_OK;
}
