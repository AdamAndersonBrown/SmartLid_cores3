#include "speaker_manager.h"
#include "touch_manager.h"
#include "servo_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "common_defs.h"
#include "freertos/semphr.h"
extern SemaphoreHandle_t i2c_mutex;

volatile int active_event_tag = 0;
#include "display_manager.h"
extern void display_manager_wake(void);
#include "esp_wifi.h"
extern bool wifi_logging_enabled;

static const char *TAG = "TOUCH";
#define FT6336U_ADDR 0x38
#define RESET_TIME_MS 7000
#define WARN_TIME_MS  5000

void touch_task(void *pvParameters) {
    uint8_t data[5];
    int reset_held_time = 0;
    int wifi_held_time = 0;
    int miss_count = 0;
    int last_tag = -1;

    while(1) {
        uint8_t reg = 0x02; 
        esp_err_t err = ESP_FAIL;
        if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            err = i2c_master_write_read_device(I2C_NUM_0, FT6336U_ADDR, &reg, 1, data, 5, pdMS_TO_TICKS(10));
            xSemaphoreGive(i2c_mutex);
        }
        
        bool is_touched = false;
        if (err == ESP_OK) {
            uint8_t touch_points = data[0] & 0x0F;
            if (touch_points > 0 && touch_points <= 2) {
                uint16_t raw_x = ((data[1] & 0x0F) << 8) | data[2];
                uint16_t y = ((data[3] & 0x0F) << 8) | data[4];
                
                // HARDWARE FIX: Invert X-Axis to match physical Core2 LCD orientation
                uint16_t x = raw_x; 
                
                display_manager_wake();
                is_touched = true;
                miss_count = 0;
                
                if (y >= 20 && y <= 100) {
                    if (x < 100) { servo_set_manual(0); }
                    else if (x > 220) { servo_set_manual(180); }
                    else { servo_trigger_unlock_sequence(); }
                } else if (y > 240) {
                    
                    if (x < 100) { 
                        // LEFT CHIN: Factory Reset (7 seconds)
                        active_event_tag = 0;
                        reset_held_time += 50;
                        wifi_held_time = 0; // Clear other timers
                        display_manager_draw_reset_progress((reset_held_time * 100) / RESET_TIME_MS, (reset_held_time >= WARN_TIME_MS));
                        
                        if (reset_held_time >= RESET_TIME_MS) {
                            ESP_LOGE(TAG, "!!! FACTORY RESET TRIGGERED BY TOUCH !!!");
                            xEventGroupSetBits(wifi_event_group, FACTORY_RESET_BIT);
                            reset_held_time = 0; 
                            vTaskDelay(pdMS_TO_TICKS(5000));
                        }
                    } else if (x >= 100 && x < 220) {
                        // MIDDLE CHIN: Disabled per UI removal
                        reset_held_time = 0; wifi_held_time = 0;
                    } else if (x >= 220) { 
                        // RIGHT CHIN: Tag 2 & Wi-Fi Toggle (3 seconds)
                        active_event_tag = 2;
                        reset_held_time = 0;
                        display_manager_draw_reset_progress(0, false);
                        
                        wifi_held_time += 50; // Accumulate time
                        if (wifi_held_time >= 3000) {
                            wifi_logging_enabled = !wifi_logging_enabled;
                            if (wifi_logging_enabled) {
                                ESP_LOGW(TAG, "Diagnostic Mode: Wi-Fi WAKING UP");
                                esp_wifi_start();
                                display_manager_fill_screen(0x001F); // COLOR_BLUE
                            } else {
                                ESP_LOGW(TAG, "Deployment Mode: Wi-Fi KILLED");
                                esp_wifi_disconnect(); esp_wifi_stop(); 
                                display_manager_fill_screen(0x0000); // COLOR_BLACK
                            }
                            wifi_held_time = 0; 
                            vTaskDelay(pdMS_TO_TICKS(1000)); // Debounce toggle
                        }
                    }
                }
            }
        }
        
        if (!is_touched) {
            miss_count++;
            if (miss_count > 5) { // 250ms debounce
                active_event_tag = 0;
                wifi_held_time = 0;
                if (reset_held_time > 0) {
                    reset_held_time = 0;
                    display_manager_draw_reset_progress(0, false);
                }
            }
        }

        if (active_event_tag != last_tag) {
            display_manager_draw_tag(active_event_tag);
            last_tag = active_event_tag;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void touch_manager_init(void) {
    xTaskCreate(touch_task, "touch_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Capacitive Touch UI and Tagger initialized.");
}