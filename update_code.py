import os

def patch_file(filepath, search_str, replace_str):
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found. Please run this from the project root.")
        return
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    if search_str not in content:
        if replace_str in content:
            print(f"Skipping {filepath} - Already patched.")
        else:
            print(f"Warning: Target string missing in {filepath}. Code may have been modified differently.")
        return
    content = content.replace(search_str, replace_str)
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"Successfully patched {filepath}")

pm_c = os.path.join("main", "core", "power_manager_s3.c")

# Matching the exact text you provided in your file dump
task_old = """static void power_manager_task(void *pvParameters) {
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
}"""

task_new = """extern void display_manager_draw_battery(int percent, bool is_charging);

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
}"""
patch_file(pm_c, task_old, task_new)