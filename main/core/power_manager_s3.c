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
    if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) {
        uint8_t buf[2] = {reg, val};
        i2c_master_write_to_device(I2C_PORT, addr, buf, 2, pdMS_TO_TICKS(50));
        xSemaphoreGive(i2c_mutex);
    }
}

static uint8_t read_reg(uint8_t addr, uint8_t reg) {
    uint8_t val = 0xBF; // FAILSAFE: Prevents killing PMIC logic if I2C bus collides
    if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) {
        i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, &val, 1, pdMS_TO_TICKS(100));
        xSemaphoreGive(i2c_mutex);
    }
    return val;
}

static bool is_pre_initialized = false;
void power_manager_s3_pre_init(void) {
    if (is_pre_initialized) return;
    is_pre_initialized = true;

    if (i2c_mutex == NULL) i2c_mutex = xSemaphoreCreateMutex();
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 12,
        .scl_io_num = 11,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, // 100kHz stabilizes the highly-congested CoreS3 shared bus
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

extern void display_manager_draw_battery(int percent, bool is_charging);

static void power_manager_task(void *pvParameters) {
    while(1) {
        if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) {
            uint8_t reg16 = 0x16, val16 = 0;
            uint8_t reg62 = 0x62, val62 = 0;
            uint8_t reg_bat = 0xA4, val_bat = 0; // PMIC_REG_BAT_PERCENT
            uint8_t reg_pmu = 0x00, val_pmu = 0; // PMIC_REG_PMU_STATUS
            
            // Use 50ms timeouts to prevent I2C bus hangs
            esp_err_t err16 = i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg16, 1, &val16, 1, pdMS_TO_TICKS(50));
            esp_err_t err62 = i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg62, 1, &val62, 1, pdMS_TO_TICKS(50));
            esp_err_t err_bat = i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg_bat, 1, &val_bat, 1, pdMS_TO_TICKS(50));
            esp_err_t err_pmu = i2c_master_write_read_device(I2C_PORT, AXP2101_ADDR, &reg_pmu, 1, &val_pmu, 1, pdMS_TO_TICKS(50));
            
            // Defend the limits against silent transient PMIC resets
            // Lower limits to prevent PMIC UVLO (brownout) on weak USB ports
            if (err16 == ESP_OK && val16 != 0x03) { // 1.0A VBUS Limit
                uint8_t buf[2] = {0x16, 0x03};
                i2c_master_write_to_device(I2C_PORT, AXP2101_ADDR, buf, 2, pdMS_TO_TICKS(50));
            }
            if (err62 == ESP_OK && val62 != 0x0A) { // 300mA Charge Limit
                uint8_t buf[2] = {0x62, 0x0A};
                i2c_master_write_to_device(I2C_PORT, AXP2101_ADDR, buf, 2, pdMS_TO_TICKS(50));
            }
            xSemaphoreGive(i2c_mutex);
            
            // Push active telemetry safely back to the LVGL Header
            if (err_bat == ESP_OK) {
                // Strip the Data Valid bit (0x80) from the percentage
                uint8_t percent = val_bat & 0x7F;
                if (percent <= 100) { 
                    bool is_charging = (err_pmu == ESP_OK) ? ((val_pmu & 0x20) != 0) : false;
                    display_manager_draw_battery(percent, is_charging);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static bool is_initialized = false;
void power_manager_s3_init(void) {
    if (is_initialized) return;
    is_initialized = true;

    ESP_LOGI(TAG, "Initializing CoreS3 Power and AW9523B IO Expander...");
    vTaskDelay(pdMS_TO_TICKS(50));

    // Enable necessary PMIC LDOs (ALDO2, ALDO4, BLDO1, BLDO2, DLDO1) for LCD logic and CoreS3 hardware
    write_reg(AXP2101_ADDR, 0x93, 0x1C); // ALDO2
    write_reg(AXP2101_ADDR, 0x95, 0x1C); // ALDO4
    write_reg(AXP2101_ADDR, 0x96, 0x1C); // BLDO1 (LCD AVDD)
    write_reg(AXP2101_ADDR, 0x97, 0x1C); // BLDO2
    write_reg(AXP2101_ADDR, 0x99, 0x1C); // DLDO1
    
    uint8_t ldo_reg = read_reg(AXP2101_ADDR, 0x90);
    ldo_reg |= (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 7);
    write_reg(AXP2101_ADDR, 0x90, ldo_reg);
    
    vTaskDelay(pdMS_TO_TICKS(50));

    // CRITICAL: AW9523 Port 1 (0x13) MUST be in GPIO Mode. 
    // P1_0 is Backlight, P1_1 is LCD Reset. LED mode floats them!
    write_reg(AW9523_ADDR, 0x12, 0xFF); // Port 0 GPIO Mode
    write_reg(AW9523_ADDR, 0x13, 0xFF); // Port 1 GPIO Mode

    // Configure P1_0 (Backlight) and P1_1 (Reset) as Outputs (0 = Output)
    uint8_t dir_reg = read_reg(AW9523_ADDR, 0x05);
    dir_reg &= ~((1 << 0) | (1 << 1));
    write_reg(AW9523_ADDR, 0x05, dir_reg);

    // Pull Reset LOW, Backlight OFF
    uint8_t out_reg = read_reg(AW9523_ADDR, 0x03);
    out_reg &= ~((1 << 0) | (1 << 1));
    write_reg(AW9523_ADDR, 0x03, out_reg);
    
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Release Reset HIGH, Turn Backlight ON
    out_reg |= ((1 << 0) | (1 << 1));
    write_reg(AW9523_ADDR, 0x03, out_reg);
    
    // CRITICAL: Prevent Cold-Boot WiFi Brownouts
    // AXP2101 defaults to 100mA VBUS limit on a hard reset. Raise it synchronously 
    // BEFORE WiFi turns on, or the RF calibration spike will crash the ESP32.
    write_reg(AXP2101_ADDR, 0x16, 0x03); // Set VBUS Limit to 1.0A
    write_reg(AXP2101_ADDR, 0x62, 0x0A); // Set Charge Limit to 300mA

    // CRITICAL: Wake up PMIC Battery ADCs and E-Gauge (Required for Bare-Metal)
    write_reg(AXP2101_ADDR, 0x18, 0x22); // Enable Charging
    write_reg(AXP2101_ADDR, 0x27, 0x3F); // Enable all internal ADCs
    write_reg(AXP2101_ADDR, 0x69, 0x01); // Turn ON Fuel Gauge Processor
    
    vTaskDelay(pdMS_TO_TICKS(150));
    xTaskCreatePinnedToCore(power_manager_task, "power_daemon", 2048, NULL, 2, NULL, 0);
}

void cores3_set_screen_power(bool enable) {
    // CoreS3 LCD Backlight is driven by AW9523 P1_0, not PMIC DLDO1
    uint8_t out_reg = read_reg(AW9523_ADDR, 0x03);
    if (enable) out_reg |= (1 << 0);
    else        out_reg &= ~(1 << 0);
    write_reg(AW9523_ADDR, 0x03, out_reg);
}
