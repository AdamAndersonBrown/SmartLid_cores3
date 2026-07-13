// Filename: error_manager.c
#include "error_manager.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "ERROR_MANAGER";
// --- MODIFICATION: Removed NVS_NAMESPACE and NVS_KEY ---

// Cache the last known error code in RAM only
static last_error_code_t g_cached_error_code = ERR_CODE_NONE;

void error_manager_init(void) {
    if (g_cached_error_code != ERR_CODE_NONE) {
        return; // Already initialized
    }
    // --- MODIFICATION: We no longer read from NVS ---
    g_cached_error_code = ERR_CODE_NONE;
    ESP_LOGI(TAG, "Error Manager Initialized (NVS writes disabled).");
}

void error_manager_set_last_error(last_error_code_t error_code) {
    // --- MODIFICATION: Implement RAM caching only ---
    if (g_cached_error_code == error_code) {
        ESP_LOGD(TAG, "Error code %d already set in RAM.", error_code);
        return;
    }
    // --- END MODIFICATION ---

    // --- MODIFICATION: Log the error but do not write to NVS ---
    ESP_LOGW(TAG, "Runtime error code set to %d (%s)", error_code, error_manager_get_string(error_code));
    g_cached_error_code = error_code;
    // --- NVS WRITE LOGIC REMOVED ---
}

last_error_code_t error_manager_get_last_error(void) {
    // --- MODIFICATION: Always return the RAM-cached error code ---
    return g_cached_error_code;
    // --- NVS READ LOGIC REMOVED ---
}

const char* error_manager_get_string(last_error_code_t error_code) {
    switch (error_code) {
        case ERR_CODE_NONE:
            return "No error";
        case ERR_WIFI_CONNECT_FAIL:
            return "WIFI connection failed";
        case ERR_OTA_FAIL:
            return "OTA update failed";
        case ERR_I2S_INIT_FAIL:
            return "I2S initialization failed";
        case ERR_CONFIG_LOAD_FAIL:
            return "Configuration load failed";
        case ERR_RINGBUF_FAIL:
            return "Ring buffer creation failed";
        case ERR_TIME_SYNC_FAILED:
            return "Time synchronization failed";
        case ERR_CONFIG_SAVE_FAIL:
            return "Configuration save failed";
        case ERR_STREAMING_STALLED:
            return "Audio streaming stalled or timed out";
        case ERR_UNKNOWN:
        default:
            return "Unknown error";
    }
}