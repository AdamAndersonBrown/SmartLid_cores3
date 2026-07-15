#include "power_manager_s3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"

#define I2C_PORT I2C_NUM_0
#define AXP2101_ADDR 0x34
#define AW9523_ADDR  0x58

extern SemaphoreHandle_t i2c_mutex;
static const char *TAG = "POWER_S3";

static void write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    if (i2c_mutex && xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
        uint8_t buf[2] = {reg, val};
        i2c_master_write_to_device(I2C_PORT, addr, buf, 2, pdMS_TO_TICKS(100));
        xSemaphoreGive(i2c_mutex);
    }
}

static uint8_t read_reg(uint8_t addr, uint8_t reg) {
    uint8_t val = 0xBF; // FAILSAFE: Prevents killing PMIC logic if I2C bus collides
    if (i2c_mutex && xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
        i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, &val, 1, pdMS_TO_TICKS(100));
        xSemaphoreGive(i2c_mutex);
    }
    return val;
}

void power_manager_s3_pre_init(void) {
    if (i2c_mutex == NULL) i2c_mutex = xSemaphoreCreateMutex();
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 12,
        .scl_io_num = 11,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static void power_manager_task(void *pvParameters) {
    while(1) {
        if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) {
            uint8_t reg16 = 0x16, val16 = 0;
            uint8_t reg62 = 0x62, val62 = 0;
            i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg16, 1, &val16, 1, pdMS_TO_TICKS(10));
            i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg62, 1, &val62, 1, pdMS_TO_TICKS(10));
            
            // Defend the limits against silent transient PMIC resets
            // Lower limits to prevent PMIC UVLO (brownout) on weak USB ports
            if (val16 != 0x03) { // 1.0A VBUS Limit
                uint8_t buf[2] = {0x16, 0x03};
                i2c_master_write_to_device(I2C_PORT, AXP2101_ADDR, buf, 2, pdMS_TO_TICKS(10));
            }
            if (val62 != 0x0A) { // 300mA Charge Limit
                uint8_t buf[2] = {0x62, 0x0A};
                i2c_master_write_to_device(I2C_PORT, AXP2101_ADDR, buf, 2, pdMS_TO_TICKS(10));
            }
            xSemaphoreGive(i2c_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void power_manager_s3_init(void) {
    ESP_LOGI(TAG, "Initializing CoreS3 Power and AW9523B IO Expander...");
    vTaskDelay(pdMS_TO_TICKS(50));

    // Power critical 3.3v Logic (ALDO2, ALDO4, DLDO1)
    write_reg(AXP2101_ADDR, 0x93, 0x1C);
    write_reg(AXP2101_ADDR, 0x95, 0x1C);
    write_reg(AXP2101_ADDR, 0x99, 0x1C);
    
    uint8_t ldo_reg = read_reg(AXP2101_ADDR, 0x90);
    ldo_reg |= (1 << 1) | (1 << 3) | (1 << 7);
    write_reg(AXP2101_ADDR, 0x90, ldo_reg);
    
    vTaskDelay(pdMS_TO_TICKS(50));

    // CRITICAL: Force AW9523 Port 1 to GPIO Mode (Default is LED mode)
    // In LED mode, P1_1 floats as open-drain, permanently trapping the LCD in hardware reset!
    write_reg(AW9523_ADDR, 0x12, 0xFF);

    // Configure IO Expander for LCD Reset control (P1_1 Output)
    uint8_t dir_reg = read_reg(AW9523_ADDR, 0x05);
    dir_reg &= ~(1 << 1);
    write_reg(AW9523_ADDR, 0x05, dir_reg);

    // Pull LCD Reset LOW
    uint8_t out_reg = read_reg(AW9523_ADDR, 0x03);
    out_reg &= ~(1 << 1);
    write_reg(AW9523_ADDR, 0x03, out_reg);
    
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Release LCD Reset HIGH
    out_reg |= (1 << 1);
    write_reg(AW9523_ADDR, 0x03, out_reg);
    
    vTaskDelay(pdMS_TO_TICKS(150));
    xTaskCreatePinnedToCore(power_manager_task, "power_daemon", 2048, NULL, 2, NULL, 0);
}

void cores3_set_screen_power(bool enable) {
    uint8_t ldo_reg = read_reg(AXP2101_ADDR, 0x90);
    if (enable) ldo_reg |= (1 << 7);
    else        ldo_reg &= ~(1 << 7);
    write_reg(AXP2101_ADDR, 0x90, ldo_reg);
}
