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
disp_c = os.path.join("main", "hardware", "display_manager.c")

# --- 1. Fix AW9523 Port 1 (Backlight & Reset) Floating Pin Drop ---
pm_init_old = """    // Power critical 3.3v Logic (ALDO2, ALDO4, DLDO1)
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
    write_reg(AW9523_ADDR, 0x03, out_reg);"""

pm_init_new = """    // Enable necessary PMIC LDOs (ALDO2, ALDO4, BLDO1, BLDO2, DLDO1) for LCD logic and CoreS3 hardware
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
    write_reg(AW9523_ADDR, 0x03, out_reg);"""
patch_file(pm_c, pm_init_old, pm_init_new)


# --- 2. Fix Backlight Toggle Targeting ---
pm_pwr_old = """void cores3_set_screen_power(bool enable) {
    uint8_t ldo_reg = read_reg(AXP2101_ADDR, 0x90);
    if (enable) ldo_reg |= (1 << 7);
    else        ldo_reg &= ~(1 << 7);
    write_reg(AXP2101_ADDR, 0x90, ldo_reg);
}"""

pm_pwr_new = """void cores3_set_screen_power(bool enable) {
    // CoreS3 LCD Backlight is driven by AW9523 P1_0, not PMIC DLDO1
    uint8_t out_reg = read_reg(AW9523_ADDR, 0x03);
    if (enable) out_reg |= (1 << 0);
    else        out_reg &= ~(1 << 0);
    write_reg(AW9523_ADDR, 0x03, out_reg);
}"""
patch_file(pm_c, pm_pwr_old, pm_pwr_new)


# --- 3. Stop Corrupting the SPI LCD Matrix (Backlight Sleep ONLY) ---
disp_sleep_old = """static void display_sleep_task(void *pvParam) {
    while(1) {
        if (screen_on) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_wake_time) > pdMS_TO_TICKS(10000)) { 
                if (qr_bg == NULL) { 
                    screen_on = false; 
                    vTaskDelay(pdMS_TO_TICKS(50)); 
                    esp_lcd_panel_disp_on_off(panel_handle, false); 
                    core2_set_screen_power(false); 
                    ESP_LOGI("POWER", "Screen Sleeping (10s Idle)");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}"""

disp_sleep_new = """static void display_sleep_task(void *pvParam) {
    while(1) {
        if (screen_on) {
            TickType_t now = xTaskGetTickCount();
            // 15 seconds to clear ML model loading time during boot
            if ((now - last_wake_time) > pdMS_TO_TICKS(15000)) { 
                if (qr_bg == NULL) { 
                    screen_on = false; 
                    core2_set_screen_power(false); // Sever backlight ONLY. Leave LCD logic running.
                    ESP_LOGI("POWER", "Screen Sleeping (15s Idle)");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}"""
patch_file(disp_c, disp_sleep_old, disp_sleep_new)

disp_wake_old = """void display_manager_wake(void) {
    last_wake_time = xTaskGetTickCount();
    if (!screen_on) {
        core2_set_screen_power(true);
        esp_lcd_panel_disp_on_off(panel_handle, true);
        vTaskDelay(pdMS_TO_TICKS(150)); // Hardware wake delay to prevent SPI lockup
        screen_on = true;
        
        // Force LVGL to repaint the GRAM upon waking
        if (lvgl_mux && xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_obj_invalidate(lv_scr_act());
            xSemaphoreGive(lvgl_mux);
        }
        
        display_manager_draw_servo_buttons();
        ESP_LOGI("POWER", "Screen Woken Up");
    }
}"""

disp_wake_new = """void display_manager_wake(void) {
    last_wake_time = xTaskGetTickCount();
    if (!screen_on) {
        core2_set_screen_power(true); // Restore backlight
        screen_on = true;
        
        // Force LVGL to repaint upon waking just in case
        if (lvgl_mux && xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_obj_invalidate(lv_scr_act());
            xSemaphoreGive(lvgl_mux);
        }
        ESP_LOGI("POWER", "Screen Woken Up");
    }
}"""
patch_file(disp_c, disp_wake_old, disp_wake_new)


# --- 4. Safely Allocate QR Code Overlay to Prevent Rendering White Blocks ---
qr_mem_old = """        if (!qr_img_data) {
            // Attempt to allocate DMA capable memory first, fallback to standard heap
            qr_img_data = heap_caps_malloc(img_w * img_h * sizeof(uint16_t), MALLOC_CAP_DMA);
            if (!qr_img_data) qr_img_data = malloc(img_w * img_h * sizeof(uint16_t));
        }

        if (qr_img_data) {"""

qr_mem_new = """        if (!qr_img_data) {
            qr_img_data = heap_caps_malloc(img_w * img_h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
            if (!qr_img_data) qr_img_data = malloc(img_w * img_h * sizeof(uint16_t));
        }

        if (!qr_img_data) {
            lv_obj_del(qr_bg);
            qr_bg = NULL;
            xSemaphoreGive(lvgl_mux);
            return;
        }

        if (qr_img_data) {"""
patch_file(disp_c, qr_mem_old, qr_mem_new)


# --- 5. Prevent LVGL Z-Index Occlusion ("Screen Overwriting") ---
force_bg_old = """    // SURGICAL FIX: Force LVGL to paint a physical black rectangle over the entire screen.
    // This prevents old ILI9341 GRAM contents (like the QR code) from surviving a software reboot.
    lv_obj_t * force_bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(force_bg, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(force_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(force_bg, 0, 0);
    lv_obj_set_style_radius(force_bg, 0, 0);
    lv_obj_clear_flag(force_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(force_bg);"""

force_bg_new = """    // force_bg physically removed to prevent LVGL Z-Index rendering occlusion."""
patch_file(disp_c, force_bg_old, force_bg_new)


# --- 6. Prevent SPI DMA Cache Deadlocks ---
dma_cb_old = """static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {"""
dma_cb_new = """#include "esp_attr.h"
static bool IRAM_ATTR notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {"""
patch_file(disp_c, dma_cb_old, dma_cb_new)