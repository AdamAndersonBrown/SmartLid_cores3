#include "servo_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/mcpwm_prelude.h"
#include "esp_pm.h"
#include "esp_rom_sys.h"

static const char *TAG = "SERVO";
static SemaphoreHandle_t latch_semaphore = NULL;
static mcpwm_cmpr_handle_t comparator = NULL;
static int current_servo_angle = 0;
static esp_pm_lock_handle_t servo_pm_lock = NULL;

// MG996R typical pulse widths
#define SERVO_MIN_PULSEWIDTH_US 500  // 0 degrees
#define SERVO_MAX_PULSEWIDTH_US 2500 // 180 degrees
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000 // 1MHz, 1us per tick
#define SERVO_TIMEBASE_PERIOD 20000    // 20000 ticks = 20ms = 50Hz

static inline uint32_t angle_to_compare(int angle) {
    return (angle * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / 180) + SERVO_MIN_PULSEWIDTH_US;
}

// --- Smooth Interpolation Engine ---
static void servo_move_smooth(int target_angle, int delay_ms) {
    // Hard mechanical limits to prevent piano wire binding
    if (target_angle < 0) target_angle = 0;
    if (target_angle > 180) target_angle = 180;

    // Prevent Light Sleep from killing the APB Clock during active holding/sweeping
    if (current_servo_angle == 0 && target_angle > 0) {
        if (servo_pm_lock) esp_pm_lock_acquire(servo_pm_lock);
    }

    if (comparator != NULL) {
        // Sync interpolation strictly to the 50Hz (20ms) PWM frame to prevent shadow register jitter
        int step_size = 20 / delay_ms; 
        if (step_size < 1) step_size = 1;
        
        while (current_servo_angle != target_angle) {
            if (target_angle > current_servo_angle) {
                current_servo_angle += step_size;
                if (current_servo_angle > target_angle) current_servo_angle = target_angle;
            } else {
                current_servo_angle -= step_size;
                if (current_servo_angle < target_angle) current_servo_angle = target_angle;
            }
            mcpwm_comparator_set_compare_value(comparator, angle_to_compare(current_servo_angle));
            vTaskDelay(pdMS_TO_TICKS(20)); // Yield exactly 1 PWM frame
        }
        
        // --- NEW: SOFTWARE LIMP MODE ---
        if (target_angle == 0) {
            vTaskDelay(pdMS_TO_TICKS(150)); // Allow mechanical latch/springs to settle
            mcpwm_comparator_set_compare_value(comparator, 0); // Drop duty cycle to 0
            ESP_LOGI(TAG, "Servo returned to 0-Duty Limp Mode");
            if (servo_pm_lock) esp_pm_lock_release(servo_pm_lock); // Safe to sleep again
        }
    } else {
        current_servo_angle = target_angle;
    }
}

static void servo_task(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(latch_semaphore, portMAX_DELAY) == pdTRUE) {
            ESP_LOGW(TAG, "Actuating Latch Assembly...");
            
            // Sweep to 90 degrees (8ms per degree = ~720ms total travel time)
            servo_move_smooth(180, 2); 
            vTaskDelay(pdMS_TO_TICKS(1500)); // Hold open against springs
            
            ESP_LOGI(TAG, "Releasing Latch...");
            // Sweep back to 0 degrees safely
            servo_move_smooth(0, 2); 
        }
    }
}

void servo_manager_init(void) {
    ESP_LOGI(TAG, "Initializing Smooth MCPWM V5 Driver on GPIO 33");
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "servo_lck", &servo_pm_lock);
    
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
        .period_ticks = SERVO_TIMEBASE_PERIOD,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    mcpwm_oper_handle_t oper = NULL;
    mcpwm_operator_config_t oper_config = { .group_id = 0 };
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_config, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    mcpwm_comparator_config_t comparator_config = { .flags.update_cmp_on_tez = true };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

    mcpwm_gen_handle_t generator = NULL;
    mcpwm_generator_config_t generator_config = { .gen_gpio_num = 8 }; // CoreS3 Port A (GPIO 33 is strictly internal I2S on S3!)
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));

    current_servo_angle = 0;
    
    // 0 duty cycle = Limp mode to prevent violent boot snap
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, 0));

    latch_semaphore = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(servo_task, "servo_task", 4096, NULL, 3, NULL, 1);
}

void servo_actuate_latch(void) {
    if (latch_semaphore != NULL) xSemaphoreGive(latch_semaphore);
}

void servo_set_manual(int target_angle) {
    // 3ms per degree gives a 5x faster sweep for active ML unlocks for manual adjustments
    servo_move_smooth(target_angle, 3);
}

static bool is_unlocking = false;

static void unlock_sequence_task(void *pvParameters) {
    is_unlocking = true;
    ESP_LOGI(TAG, "Unlock Sequence Triggered: Sweeping CW (180 deg)");
    servo_set_manual(180);
    
    // Non-blocking wait in the background
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    ESP_LOGI(TAG, "Unlock Sequence Concluding: Sweeping CCW (0 deg)");
    servo_set_manual(0); // MUST hit 0 to trigger Limp Mode & release PM Lock
    
    is_unlocking = false;
    vTaskDelete(NULL); // Task deletes itself to free memory
}

void servo_trigger_unlock_sequence(void) {
    if (!is_unlocking) {
        xTaskCreatePinnedToCore(unlock_sequence_task, "unlock_task", 2048, NULL, 3, NULL, 1);
    } else {
        ESP_LOGW(TAG, "Unlock sequence already in progress, ignoring tap.");
    }
}
