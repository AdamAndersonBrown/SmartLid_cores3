#include "inference_manager.h"
#include "speaker_manager.h"
#include "servo_manager.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
extern void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport);
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "common_defs.h"

#include "display_manager.h"
#include "touch_manager.h"
#include "wifi_prov_handler.h"

void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport);

#include "led_manager.h"
#include "error_manager.h"
#include "imu_telemetry_task.h"

static const char *TAG = "MAIN";
EventGroupHandle_t wifi_event_group;
QueueHandle_t imu_queue;

#include "esp_pm.h"
#include "esp_sleep.h"
#include "power_manager_s3.h"

bool wifi_logging_enabled = true;

SemaphoreHandle_t i2c_mutex = NULL;

void app_main(void) {
    // --- CORE S3 DEEP SLEEP ROUTER ---
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0 || wakeup_cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "Deep Sleep Wake: IMU Motion Detected.");
        // TODO: Mount BMI270 FIFO, Allocate Tensor Arena, Run Inference
        ESP_LOGI(TAG, "Inference Complete. Returning to Deep Sleep.");
        // esp_deep_sleep_start();
        return; // Halt cold boot execution
    }
    ESP_LOGI(TAG, "Cold Boot: Initializing System...");
    // ---------------------------------
    i2c_mutex = xSemaphoreCreateMutex();
    power_manager_s3_pre_init(); // STRICT: PMIC MUST be configured before Wi-Fi or NVS to prevent brownouts!
    // Enable DFS to sleep the CPU during the 20ms IMU polling gaps
    // STRICT ENFORCEMENT: Stripped #if macro to prevent silent compilation bypass.
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80, // Clamped to 80MHz to protect the I2C APB Baud Rate
        .light_sleep_enable = false // CRITICAL FIX: Disabled for tethered PC debugging to prevent USB disconnects
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config)); // Fail loudly if Tickless Idle is missing
    ESP_LOGI(TAG, "POWER OPTIMIZATION: DFS Active. CPU dynamically downclocking to 80MHz during RTOS idle.");

    // 0. Initialize IPC Queue First to prevent Race Conditions!
    imu_queue = xQueueCreate(100, sizeof(imu_sample_t));
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    

    error_manager_init();
    led_manager_init();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_prov_mgr_config_t prov_config = { .scheme = wifi_prov_scheme_softap, .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    // --- CORES3 HARDWARE LCD RESCUE ---
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER, .sda_io_num = 12, .scl_io_num = 11,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = { .clk_speed = 400000 }
    };
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0); // Ignore return if already installed

    // AXP2101 Power Rails (Enable all ALDOs and DLDOs to 3.3V)
    uint8_t pmic_cmds[][2] = {{0x90, 0xBF}, {0x92, 0x1C}, {0x93, 0x1C}, {0x94, 0x1C}, {0x95, 0x1C}, {0x99, 0xC0}, {0x9C, 0x1C}, {0x9D, 0x1C}};
    for(int i=0; i<8; i++) i2c_master_write_to_device(I2C_NUM_0, 0x34, pmic_cmds[i], 2, pdMS_TO_TICKS(10));
    
    // AW9523 IO Expander (Force LCD Backlight and Reset HIGH)
    uint8_t aw_cmds[][2] = {{0x04, 0x00}, {0x05, 0x00}, {0x12, 0xFF}, {0x13, 0xFF}, {0x02, 0xFF}, {0x03, 0xFF}};
    for(int i=0; i<6; i++) i2c_master_write_to_device(I2C_NUM_0, 0x58, aw_cmds[i], 2, pdMS_TO_TICKS(10));
    // ----------------------------------
    
    display_manager_init();
    power_manager_s3_init(); // Boot the CoreS3 PMIC daemon
    
    if (provisioned) {
        ESP_LOGI(TAG, "Device already provisioned. Connecting...");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        display_manager_fill_screen(COLOR_BLUE); // Blue = Connecting
    } else {
        ESP_LOGI(TAG, "Device not provisioned. Starting SoftAP provisioning...");
        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, "abcd1234", service_name, NULL));
    }

    // Wait for the connection to fully establish
    
    // --- FOOLPROOF QR TRIGGER ---
    uint8_t fw_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, fw_mac);
    char fw_name[15];
    snprintf(fw_name, sizeof(fw_name), "IMU_%02X%02X%02X", fw_mac[3], fw_mac[4], fw_mac[5]);
    wifi_prov_print_qr(fw_name, "wifiprov", "abcd1234", "softap");
    // ----------------------------
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "Successfully connected to WiFi.");
    display_manager_fill_screen(COLOR_GREEN); // Green = Streaming

    // The Golden Baseline: Reboot after first-time provisioning to kill the SoftAP
    if (!provisioned) {
        ESP_LOGI(TAG, "Waiting for provisioning manager to finalize...");
        xEventGroupWaitBits(wifi_event_group, WIFI_PROV_DONE_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
        wifi_prov_mgr_deinit();
        ESP_LOGW(TAG, "Provisioning successful. Rebooting to clear network stack...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

        // Initialize Real-Time Clock via SNTP
    ESP_LOGI(TAG, "Initializing SNTP for real-time syncing...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    setenv("TZ", "PST8PDT", 1); // Pacific Time
    tzset();
    
        // STRICT FIX: Eradicated "Go Dark" Stealth Mode.
    // Wi-Fi remains active for UDP telemetry, and the LCD remains visibly operational.
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Initialization complete. Entering Active Diagnostic Mode.");
    wifi_logging_enabled = true;

    // speaker_manager_init(); // DISABLED: Frees GPIO 0 (Green LED) and 12 (I2C)
    // servo_manager_init(); // DISABLED: Frees GPIO 33 (Audio Bus)
    start_imu_telemetry_task(); // Start polling AFTER boot handling is done
    inference_manager_init();
    
    // --- Dual-Core Inter-Process Architecture ---
    // Create a 20-slot memory queue between the cores
    
    
    // Pin the heavy Machine Learning engine to Core 1 (APP CPU)
    // Leaving Core 0 (PRO CPU) completely free for high-speed Wi-Fi and Sensor Polling
    xTaskCreatePinnedToCore(inference_task, "inference_task", 8192, NULL, 5, NULL, 1);
    touch_manager_init();

    while(1) {
        // Listen for the touch driver to throw the factory reset flag
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, FACTORY_RESET_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        
        if (bits & FACTORY_RESET_BIT) {
            ESP_LOGW(TAG, "--- COMPLETE FACTORY RESET CAUGHT ---");
            display_manager_fill_screen(COLOR_WHITE); // Flash screen white for visual feedback
            
            // Format the NVS partition to erase all saved Wi-Fi credentials
            vTaskDelay(pdMS_TO_TICKS(500));
            nvs_flash_erase();
            nvs_flash_init();
            
            ESP_LOGW(TAG, "NVS Wiped. Rebooting to Provisioning Mode...");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }

        // --- BATTERY UPDATE LOGIC ---
        static int last_batt = -1;
        static bool last_charge = false;
        int batt = 0;
        bool charging = false;
        core2_get_battery_state(&batt, &charging);
        if (batt != last_batt || charging != last_charge) {
            display_manager_draw_battery(batt, charging);
            last_batt = batt;
            last_charge = charging;
        }

        // --- WIFI RSSI UPDATE LOGIC ---
        wifi_ap_record_t ap_info;
        bool wifi_conn = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
        int current_rssi = wifi_conn ? ap_info.rssi : 0;
        
        static int last_rssi = 1; // Force initial draw
        static bool last_conn = false;
        
        // Update the UI if the signal strength changes by more than 2 dBm or connection state flips
        if (abs(current_rssi - last_rssi) > 2 || wifi_conn != last_conn) {
            display_manager_draw_wifi(current_rssi, wifi_conn);
            last_rssi = current_rssi;
            last_conn = wifi_conn;
        }
    }
}
