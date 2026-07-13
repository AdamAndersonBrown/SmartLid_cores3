#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

void display_manager_init(void);
void display_manager_fill_screen(uint16_t color);
void display_manager_draw_qr(const uint8_t *qrcode, int size);

// Basic RGB565 Colors
#define COLOR_BLUE  0x001F
#define COLOR_GREEN 0x07E0
#define COLOR_BLACK 0x0000
#define COLOR_GREEN 0x07E0
#define COLOR_WHITE 0xFFFF
#define COLOR_RED   0xF800
#define COLOR_YELLOW 0xFFE0
#define COLOR_ORANGE 0xFD20

void core2_get_battery_state(int *percent, bool *is_charging);
void display_manager_draw_wifi(int rssi, bool connected);
void display_manager_draw_battery(int percent, bool is_charging);
void display_manager_draw_reset_progress(int percent, bool warning);
void display_manager_set_alert(int class_id);
void display_manager_draw_tag(int tag);

#endif // DISPLAY_MANAGER_H

void display_manager_draw_servo_buttons(void);
