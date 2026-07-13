#include "inference_manager.h"
#include <sys/time.h>
#include "esp_timer.h"
#include "common_defs.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "driver/i2c.h"

extern void display_manager_wake(void);

static const char *TAG = "IMU_TELEMETRY";
#define I2C_MASTER_NUM I2C_NUM_0
#define MPU6886_ADDR 0x68
#define UDP_BROADCAST_PORT 3333
#define STREAM_DELAY_MS 1000 // HACK: Force Light Sleep for Battery Test

// Widen the buffer to 10KB to safely hold 6000 bytes of 5-second burst data
static RingbufHandle_t telemetry_rb = NULL;
extern bool wifi_logging_enabled;

typedef struct {
    int64_t ts;
    int16_t ax; int16_t ay; int16_t az;
    int16_t gx; int16_t gy; int16_t gz;
    int tag;
} log_record_t;

// --- PRODUCER: The High-Speed Sensor Task ---
static void imu_sensor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Sensor Producer Task booted on Core %d", xPortGetCoreID());

    uint8_t write_buf[2] = {0x6B, 0x00};
    i2c_master_write_to_device(I2C_MASTER_NUM, MPU6886_ADDR, write_buf, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(500)); // ALLOW MEMS SENSORS TO STABILIZE BEFORE CALIBRATION

    uint8_t raw_data[14];
    int16_t last_ax = 0, last_ay = 0, last_az = 0;

    while (1) {
        uint8_t reg = 0x3B;
        if (i2c_master_write_read_device(I2C_MASTER_NUM, MPU6886_ADDR, &reg, 1, raw_data, 14, pdMS_TO_TICKS(10)) == ESP_OK) {
            int16_t acc_x = (raw_data[0] << 8) | raw_data[1];
            int16_t acc_y = (raw_data[2] << 8) | raw_data[3];
            int16_t acc_z = (raw_data[4] << 8) | raw_data[5];
            int16_t gyro_x = (raw_data[8] << 8) | raw_data[9];
            int16_t gyro_y = (raw_data[10] << 8) | raw_data[11];
            int16_t gyro_z = (raw_data[12] << 8) | raw_data[13];

            int16_t delta = abs(acc_x - last_ax) + abs(acc_y - last_ay) + abs(acc_z - last_az);
            if (1) { // TEMPORAL COMPRESSION DISABLED FOR ML
                imu_sample_t sample = {acc_x, acc_y, acc_z, gyro_x, gyro_y, gyro_z};
                xQueueSend(imu_queue, &sample, 0); 
            }
            if (delta > 6000) display_manager_wake(); 
            last_ax = acc_x; last_ay = acc_y; last_az = acc_z;

            struct timeval tv;
            gettimeofday(&tv, NULL);
            log_record_t record = {
                .ts = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec,
                .ax = acc_x, .ay = acc_y, .az = acc_z,
                .gx = gyro_x, .gy = gyro_y, .gz = gyro_z,
                .tag = active_event_tag
            };

            // NEW: Gate the producer
            if (wifi_logging_enabled) { 
                xRingbufferSend(telemetry_rb, &record, sizeof(log_record_t), 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(STREAM_DELAY_MS));
    }
}

// --- CONSUMER: The Network Batching Task ---
static void udp_batch_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    int broadcast_enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_BROADCAST_PORT);
    dest_addr.sin_addr.s_addr = inet_addr("192.168.86.39"); 

    char payload_chunk[1400];
    int current_len = 0;

    while (1) {
        // Sleep the network stack for exactly 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        if (!wifi_logging_enabled) {
            // Drain and discard any lingering data to prevent lockups
            size_t item_size; void *r;
            while ((r = xRingbufferReceive(telemetry_rb, &item_size, 0)) != NULL) {
                vRingbufferReturnItem(telemetry_rb, r);
            }
            continue; // Skip the UDP formatting and sendto() logic
        }

        size_t item_size;
        log_record_t *rec;
        int total_drained = 0;
        int packets_fired = 0;
        
        // Wake up and drain the entire buffer instantly
        while ((rec = (log_record_t *)xRingbufferReceive(telemetry_rb, &item_size, 0)) != NULL) {
            
            if (current_len == 0) {
                current_len += snprintf(payload_chunk + current_len, sizeof(payload_chunk) - current_len, "[");
            } else {
                current_len += snprintf(payload_chunk + current_len, sizeof(payload_chunk) - current_len, ",");
            }

            current_len += snprintf(payload_chunk + current_len, sizeof(payload_chunk) - current_len, 
                "{\"ts\":%lld,\"ax\":%d,\"ay\":%d,\"az\":%d,\"gx\":%d,\"gy\":%d,\"gz\":%d,\"tag\":%d}", 
                rec->ts, rec->ax, rec->ay, rec->az, rec->gx, rec->gy, rec->gz, rec->tag);
            
            total_drained++;
            vRingbufferReturnItem(telemetry_rb, (void *)rec);

            // Fire packet if MTU limit reached
            if (current_len > 1200) {
                snprintf(payload_chunk + current_len, sizeof(payload_chunk) - current_len, "]");
                sendto(sock, payload_chunk, strlen(payload_chunk), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                current_len = 0;
                packets_fired++;
                vTaskDelay(pdMS_TO_TICKS(20)); // Router breather
            }
        }

        // Flush any remaining data
        if (current_len > 0) {
            snprintf(payload_chunk + current_len, sizeof(payload_chunk) - current_len, "]");
            sendto(sock, payload_chunk, strlen(payload_chunk), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            current_len = 0;
            packets_fired++;
        }

        // Print exact transparent metrics to the serial monitor!
        ESP_LOGI(TAG, "Network Wake: Drained %d logs across %d UDP packets. Returning to sleep.", total_drained, packets_fired);
    }
}

esp_err_t start_imu_telemetry_task(void) {
    telemetry_rb = xRingbufferCreate(10240, RINGBUF_TYPE_NOSPLIT); // 10KB Buffer
    if (telemetry_rb == NULL) return ESP_FAIL;

    xTaskCreatePinnedToCore(imu_sensor_task, "imu_sensor_task", 4096, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(udp_batch_task, "udp_batch_task", 8192, NULL, 5, NULL, 0);
    return ESP_OK;
}