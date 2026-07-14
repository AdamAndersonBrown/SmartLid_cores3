#include "power_manager_s3.h"
#include "esp_log.h"
#include "common_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"

static const char *TAG = "POWER_DAEMON_S3";
#define PMIC_I2C_ADDR 0x34
#define I2C_PORT I2C_NUM_0 

static void sever_extraneous_hardware() {
    uint8_t aldo_reg = 0x90, aldo_val = 0;
    if (i2c_master_write_read_device(I2C_PORT, PMIC_I2C_ADDR, &aldo_reg, 1, &aldo_val, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
        uint8_t optimized_val = (aldo_val & 0xF0) | 0x0A; 
        if (aldo_val != optimized_val) {
            uint8_t pmic_data[2] = {aldo_reg, optimized_val};
            i2c_master_write_to_device(I2C_PORT, PMIC_I2C_ADDR, pmic_data, 2, pdMS_TO_TICKS(100));
        }
    }
}

static void open_charging_floodgates() {
    uint8_t reg_vhold = 0x15, val_vhold = 0;
    if (i2c_master_write_read_device(I2C_PORT, PMIC_I2C_ADDR, &reg_vhold, 1, &val_vhold, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
        val_vhold = (val_vhold & 0xF0) | 0x02; 
        uint8_t write_data[2] = {reg_vhold, val_vhold};
        i2c_master_write_to_device(I2C_PORT, PMIC_I2C_ADDR, write_data, 2, pdMS_TO_TICKS(100));
    }

    uint8_t reg_vbus = 0x16, val_vbus = 0;
    if (i2c_master_write_read_device(I2C_PORT, PMIC_I2C_ADDR, &reg_vbus, 1, &val_vbus, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
        val_vbus = (val_vbus & 0xF8) | 0x04; // 1.5A
        uint8_t write_data[2] = {reg_vbus, val_vbus};
        i2c_master_write_to_device(I2C_PORT, PMIC_I2C_ADDR, write_data, 2, pdMS_TO_TICKS(100));
    }

    uint8_t reg_chg = 0x18, val_chg = 0;
    if (i2c_master_write_read_device(I2C_PORT, PMIC_I2C_ADDR, &reg_chg, 1, &val_chg, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
        val_chg = val_chg | 0x02;
        uint8_t write_data[2] = {reg_chg, val_chg};
        i2c_master_write_to_device(I2C_PORT, PMIC_I2C_ADDR, write_data, 2, pdMS_TO_TICKS(100));
    }

    uint8_t reg_cc = 0x62, val_cc = 0;
    if (i2c_master_write_read_device(I2C_PORT, PMIC_I2C_ADDR, &reg_cc, 1, &val_cc, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
        val_cc = 0x13; // 1.0A limit (Thermal safe)
        uint8_t write_data[2] = {reg_cc, val_cc};
        i2c_master_write_to_device(I2C_PORT, PMIC_I2C_ADDR, write_data, 2, pdMS_TO_TICKS(100));
    }
}

void power_manager_s3_pre_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 12,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = 11,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
    sever_extraneous_hardware();
    open_charging_floodgates();
}

static void power_manager_task(void *pvParameters) {
    ESP_LOGI(TAG, "Power Manager Daemon Booted on Core 0.");
    uint32_t tick_count = 0;
    
    while(1) {
        if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (tick_count % 10 == 0) {
                // 10Hz OTG Boost Suppression
                uint8_t aw_conf_read = 0x04, aw_conf_val = 0;
                if (i2c_master_write_read_device(I2C_PORT, 0x58, &aw_conf_read, 1, &aw_conf_val, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
                    uint8_t aw_conf_write[2] = {0x04, (uint8_t)(aw_conf_val & 0xDF)}; 
                    i2c_master_write_to_device(I2C_PORT, 0x58, aw_conf_write, 2, pdMS_TO_TICKS(100));
                }

                uint8_t aw_out_read = 0x02, aw_out_val = 0;
                if (i2c_master_write_read_device(I2C_PORT, 0x58, &aw_out_read, 1, &aw_out_val, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
                    uint8_t aw_out_write[2] = {0x02, (uint8_t)(aw_out_val & 0xDF)}; 
                    i2c_master_write_to_device(I2C_PORT, 0x58, aw_out_write, 2, pdMS_TO_TICKS(100));
                }
                
                // 1Hz Settings Audit
                uint8_t reg16 = 0x16, val16 = 0;
                if (i2c_master_write_read_device(I2C_PORT, PMIC_I2C_ADDR, &reg16, 1, &val16, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
                    if ((val16 & 0x07) != 0x04) {
                        uint8_t write16[2] = {0x16, (uint8_t)((val16 & 0xF8) | 0x04)};
                        i2c_master_write_to_device(I2C_PORT, PMIC_I2C_ADDR, write16, 2, pdMS_TO_TICKS(100));
                    }
                }
                
                uint8_t reg62 = 0x62, val62 = 0;
                if (i2c_master_write_read_device(I2C_PORT, PMIC_I2C_ADDR, &reg62, 1, &val62, 1, pdMS_TO_TICKS(100)) == ESP_OK) {
                    if (val62 != 0x13) {
                        uint8_t write62[2] = {0x62, 0x13};
                        i2c_master_write_to_device(I2C_PORT, PMIC_I2C_ADDR, write62, 2, pdMS_TO_TICKS(100));
                    }
                }
            }
            xSemaphoreGive(i2c_mutex);
        }
        
        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void power_manager_s3_init(void) {
    xTaskCreatePinnedToCore(power_manager_task, "power_daemon", 4096, NULL, 2, NULL, 0);
}
