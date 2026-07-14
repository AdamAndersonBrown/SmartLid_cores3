#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
extern SemaphoreHandle_t i2c_mutex; // NEW: Global I2C Mutex
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

// --- ML & Telemetry Parameters ---
#define WINDOW_SIZE 100
#define NUM_FEATURES 6
#define NORMALIZATION_FACTOR 32768.0f
#define POLLING_RATE_MS 50

// --- Inference Engine Parameters ---
#define TRIGGER_COOLDOWN_MS 3000
#define CONFIDENCE_THRESHOLD 0.85f
#define TENSOR_ARENA_SIZE 49152 // 48 * 1024 aligned with inference_manager.cpp


#include "freertos/event_groups.h"

// --- Global Wi-Fi & Provisioning Event Bits ---
#ifndef WIFI_CONNECTED_BIT
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_PROV_DONE_BIT BIT1
#define FACTORY_RESET_BIT  BIT2
#define WIFI_FAIL_BIT      BIT3
#endif

// --- Shared State Variables ---
extern EventGroupHandle_t wifi_event_group;
extern volatile int active_event_tag;


#include "freertos/queue.h"

// --- Dual-Core Inter-Process Communication ---
typedef struct {
    int16_t ax; int16_t ay; int16_t az;
    int16_t gx; int16_t gy; int16_t gz;
} imu_sample_t;
extern QueueHandle_t imu_queue;

// --- Hardware Types ---
typedef enum {
    LED_STATE_IDLE = 0,
    LED_STATE_WIFI_CONNECTING,
    LED_STATE_WIFI_CONNECTED,
    LED_STATE_PROV_STARTED,
    LED_STATE_ERROR,
    LED_STATE_PROVISIONING_ACTIVE,
    LED_STATE_PROVISIONING_CONNECTING,
    LED_STATE_WIFI_CONNECTED_IDLE
} led_state_t;

#endif // COMMON_DEFS_H
