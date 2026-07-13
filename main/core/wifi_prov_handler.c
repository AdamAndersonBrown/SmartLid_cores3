#include <stdio.h>
#include <string.h>
#include <stdlib.h> // Required for strdup, free

// Core Includes
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h" // Needed for Kconfig checks
#include "nvs_flash.h"
// Provisioning Includes
#include "wifi_provisioning/manager.h"
#ifdef CONFIG_PROV_TRANSPORT_BLE
#include "wifi_provisioning/scheme_ble.h"
#include "protocomm_ble.h"
#endif /* CONFIG_PROV_TRANSPORT_BLE */
#ifdef CONFIG_PROV_TRANSPORT_SOFTAP
#include "wifi_provisioning/scheme_softap.h"
#endif /* CONFIG_PROV_TRANSPORT_SOFTAP */
#include "qrcode.h"
#include "display_manager.h"

// Project Includes
#include "common_defs.h"
#include "wifi_prov_handler.h"
#include "led_manager.h"
#include "app_main.h" // For access to global wifi_event_group

// Logging Tag specific to this file
static const char *TAG = "WIFI_PROV";

extern bool wifi_logging_enabled;

// --- Security Version 2 Helpers (if enabled) ---
#ifdef CONFIG_PROV_SECURITY_VERSION_2

static const char* WIFI_PROV_NVS_NAMESPACE = "prov";
static const char* WIFI_PROV_NVS_SALT_KEY = "salt";
static const char* WIFI_PROV_NVS_VERIFIER_KEY = "verifier";

static uint8_t nvs_salt[16];
static uint8_t nvs_verifier[384];


#if CONFIG_PROV_SEC2_DEV_MODE
// --- Hardcoded Sec2 Dev Mode Credentials (Fallback) ---
static const char sec2_salt[16] = {
    0x03, 0x6e, 0xe0, 0xc7, 0xbc, 0xb9, 0xed, 0xa8, 0x4c, 0x9e, 0xac, 0x97, 0xd9, 0x3d, 0xec, 0xf4
};
static const char sec2_verifier[384] = {
    0x7c, 0x7c, 0x85, 0x47, 0x65, 0x08, 0x94, 0x6d, 0xd6, 0x36, 0xaf, 0x37, 0xd7, 0xe8, 0x91, 0x43,
    0x78, 0xcf, 0xfd, 0x61, 0x6c, 0x59, 0xd2, 0xf8, 0x39, 0x08, 0x12, 0x72, 0x38, 0xde, 0x9e, 0x24,
    0xa4, 0x70, 0x26, 0x1c, 0xdf, 0xa9, 0x03, 0xc2, 0xb2, 0x70, 0xe7, 0xb1, 0x32, 0x24, 0xda, 0x11,
    0x1d, 0x97, 0x18, 0xdc, 0x60, 0x72, 0x08, 0xcc, 0x9a, 0xc9, 0x0c, 0x48, 0x27, 0xe2, 0xae, 0x89,
    0xaa, 0x16, 0x25, 0xb8, 0x04, 0xd2, 0x1a, 0x9b, 0x3a, 0x8f, 0x37, 0xf6, 0xe4, 0x3a, 0x71, 0x2e,
    0xe1, 0x27, 0x86, 0x6e, 0xad, 0xce, 0x28, 0xff, 0x54, 0x46, 0x60, 0x1f, 0xb9, 0x96, 0x87, 0xdc,
    0x57, 0x40, 0xa7, 0xd4, 0x6c, 0xc9, 0x77, 0x54, 0xdc, 0x16, 0x82, 0xf0, 0xed, 0x35, 0x6a, 0xc4,
    0x70, 0xad, 0x3d, 0x90, 0xb5, 0x81, 0x94, 0x70, 0xd7, 0xbc, 0x65, 0xb2, 0xd5, 0x18, 0xe0, 0x2e,
    0xc3, 0xa5, 0xf9, 0x68, 0xdd, 0x64, 0x7b, 0xb8, 0xb7, 0x3c, 0x9c, 0xfc, 0x00, 0xd8, 0x71, 0x7e,
    0xb7, 0x9a, 0x7c, 0xb1, 0xb7, 0xc2, 0xc3, 0x18, 0x34, 0x29, 0x32, 0x43, 0x3e, 0x00, 0x99, 0xe9,
    0x82, 0x94, 0xe3, 0xd8, 0x2a, 0xb0, 0x96, 0x29, 0xb7, 0xdf, 0x0e, 0x5f, 0x08, 0x33, 0x40, 0x76,
    0x52, 0x91, 0x32, 0x00, 0x9f, 0x97, 0x2c, 0x89, 0x6c, 0x39, 0x1e, 0xc8, 0x28, 0x05, 0x44, 0x17,
    0x3f, 0x68, 0x02, 0x8a, 0x9f, 0x44, 0x61, 0xd1, 0xf5, 0xa1, 0x7e, 0x5a, 0x70, 0xd2, 0xc7, 0x23,
    0x81, 0xcb, 0x38, 0x68, 0xe4, 0x2c, 0x20, 0xbc, 0x40, 0x57, 0x76, 0x17, 0xbd, 0x08, 0xb8, 0x96,
    0xbc, 0x26, 0xeb, 0x32, 0x46, 0x69, 0x35, 0x05, 0x8c, 0x15, 0x70, 0xd9, 0x1b, 0xe9, 0xbe, 0xcc,
    0xa9, 0x38, 0xa6, 0x67, 0xf0, 0xad, 0x50, 0x13, 0x19, 0x72, 0x64, 0xbf, 0x52, 0xc2, 0x34, 0xe2,
    0x1b, 0x11, 0x79, 0x74, 0x72, 0xbd, 0x34, 0x5b, 0xb1, 0xe2, 0xfd, 0x66, 0x73, 0xfe, 0x71, 0x64,
    0x74, 0xd0, 0x4e, 0xbc, 0x51, 0x24, 0x19, 0x40, 0x87, 0x0e, 0x92, 0x40, 0xe6, 0x21, 0xe7, 0x2d,
    0x4e, 0x37, 0x76, 0x2f, 0x2e, 0xe2, 0x68, 0xc7, 0x89, 0xe8, 0x32, 0x13, 0x42, 0x06, 0x84, 0x84,
    0x53, 0x4a, 0xb3, 0x0c, 0x1b, 0x4c, 0x8d, 0x1c, 0x51, 0x97, 0x19, 0xab, 0xae, 0x77, 0xff, 0xdb,
    0xec, 0xf0, 0x10, 0x95, 0x34, 0x33, 0x6b, 0xcb, 0x3e, 0x84, 0x0f, 0xb9, 0xd8, 0x5f, 0xb8, 0xa0,
    0xb8, 0x55, 0x53, 0x3e, 0x70, 0xf7, 0x18, 0xf5, 0xce, 0x7b, 0x4e, 0xbf, 0x27, 0xce, 0xce, 0xa8,
    0xb3, 0xbe, 0x40, 0xc5, 0xc5, 0x32, 0x29, 0x3e, 0x71, 0x64, 0x9e, 0xde, 0x8c, 0xf6, 0x75, 0xa1,
    0xe6, 0xf6, 0x53, 0xc8, 0x31, 0xa8, 0x78, 0xde, 0x50, 0x40, 0xf7, 0x62, 0xde, 0x36, 0xb2, 0xba
};
#endif // CONFIG_PROV_SEC2_DEV_MODE

esp_err_t example_get_sec2_salt(const char **salt, uint16_t *salt_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_PROV_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(nvs_salt);
        err = nvs_get_blob(nvs_handle, WIFI_PROV_NVS_SALT_KEY, nvs_salt, &required_size);
        nvs_close(nvs_handle);

        if (err == ESP_OK && required_size == sizeof(nvs_salt)) {
            ESP_LOGI(TAG, "Sec2: Using salt from NVS.");
            *salt = (const char*)nvs_salt;
            *salt_len = required_size;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Sec2: Failed to get salt from NVS (%s). Falling back.", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "Sec2: Could not open NVS namespace '%s' (%s). Falling back.", WIFI_PROV_NVS_NAMESPACE, esp_err_to_name(err));
    }

#if CONFIG_PROV_SEC2_DEV_MODE
    ESP_LOGI(TAG, "Sec2: Using hardcoded development salt as fallback.");
    *salt = sec2_salt;
    *salt_len = sizeof(sec2_salt);
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Sec2: NVS salt not found and not in dev mode!");
    return ESP_FAIL;
#endif
}

esp_err_t example_get_sec2_verifier(const char **verifier, uint16_t *verifier_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_PROV_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(nvs_verifier);
        err = nvs_get_blob(nvs_handle, WIFI_PROV_NVS_VERIFIER_KEY, nvs_verifier, &required_size);
        nvs_close(nvs_handle);

        if (err == ESP_OK && required_size == sizeof(nvs_verifier)) {
            ESP_LOGI(TAG, "Sec2: Using verifier from NVS.");
            *verifier = (const char*)nvs_verifier;
            *verifier_len = required_size;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Sec2: Failed to get verifier from NVS (%s). Falling back.", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "Sec2: Could not open NVS namespace '%s' (%s). Falling back.", WIFI_PROV_NVS_NAMESPACE, esp_err_to_name(err));
    }

#if CONFIG_PROV_SEC2_DEV_MODE
    ESP_LOGI(TAG, "Sec2: Using hardcoded development verifier as fallback.");
    *verifier = sec2_verifier;
    *verifier_len = sizeof(sec2_verifier);
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Sec2: NVS verifier not found and not in dev mode!");
    return ESP_FAIL;
#endif
}
#endif // CONFIG_PROV_SECURITY_VERSION_2

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_SOFTAP   "softap"
#define PROV_TRANSPORT_BLE      "ble"
#define QRCODE_BASE_URL         "https://espressif.github.io/esp-jumpstart/qrcode.html"

static void lcd_qrcode_print(esp_qrcode_handle_t qrcode) {
    int size = esp_qrcode_get_size(qrcode);
    
    // Allocate a flat array for our display manager
    uint8_t *flat_qr = malloc(size * size);
    if (flat_qr) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                // Extract the pixel from the component's opaque handle
                flat_qr[y * size + x] = esp_qrcode_get_module(qrcode, x, y) ? 1 : 0;
            }
        }
        display_manager_draw_qr(flat_qr, size);
        free(flat_qr);
    }
}

void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport) {
    if (!name || !transport) {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }
    char payload[150] = {0};
    
    // FORCED OVERRIDE: Always use Security 1 format with Proof of Possession
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
             PROV_QR_VERSION, name, pop ? pop : "abcd1234", transport);

    ESP_LOGI(TAG, "Scan this QR code from the provisioning application for Provisioning.");
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);
    
    // Draw directly to the physical Core2 LCD using the component callback
    esp_qrcode_config_t lcd_cfg = ESP_QRCODE_CONFIG_DEFAULT();
    lcd_cfg.display_func = lcd_qrcode_print;
    esp_qrcode_generate(&lcd_cfg, payload);
    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser:\n%s?data=%s", QRCODE_BASE_URL, payload);
}

void get_device_service_name(char *service_name, size_t max) {
    uint8_t eth_mac[6];
    const char *ssid_prefix = "IMU_"; // Renamed to bypass mobile app caching
    esp_err_t err = esp_read_mac(eth_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to get STA MAC address for service name: %s. Using default.", esp_err_to_name(err));
         snprintf(service_name, max, "%sDevice", ssid_prefix);
         return;
    }
    snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
    if (inbuf) {
        ESP_LOGI(TAG, "Received custom prov data (Session %" PRIu32 "): %.*s", session_id, (int)inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory allocating custom data response");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1;

    return ESP_OK;
}


// --- Unified Event Handler ---
void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
#ifdef CONFIG_RESET_PROV_MGR_ON_FAILURE
    static int retries = 0;
#endif

    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "EVENT: Provisioning started");
                led_manager_set_state(LED_STATE_PROVISIONING_ACTIVE);
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "EVENT: Received Wi-Fi credentials SSID: %s", (const char *) cfg->ssid);
                led_manager_set_state(LED_STATE_PROVISIONING_CONNECTING);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *r = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "EVENT: Provisioning credential failed, reason: %s",
                         (*r == WIFI_PROV_STA_AUTH_ERROR) ? "Auth Failed" : "AP Not Found");
                led_manager_set_state(LED_STATE_ERROR);
                #ifdef CONFIG_RESET_PROV_MGR_ON_FAILURE
                retries++;
                ESP_LOGW(TAG, "Credential Fail Retry: %d/%d", retries, CONFIG_PROV_MGR_MAX_RETRY_CNT);
                if (retries >= CONFIG_PROV_MGR_MAX_RETRY_CNT) {
                    ESP_LOGI(TAG, "Max retries reached. Resetting provisioning state.");
                    wifi_prov_mgr_reset_provisioning();
                    retries = 0;
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                #else
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                #endif
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "EVENT: Provisioning credential successful");
                #ifdef CONFIG_RESET_PROV_MGR_ON_FAILURE
                retries = 0;
                #endif
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "EVENT: Provisioning ended.");
                // Deinitialization is handled by app_main after a connection is fully established.
                xEventGroupSetBits(wifi_event_group, WIFI_PROV_DONE_BIT);
                break;
            default:
                break;
        }
    }
    else if (event_base == WIFI_EVENT) {
        switch (event_id) {
             case WIFI_EVENT_STA_START:
                 ESP_LOGI(TAG, "EVENT: Wi-Fi station mode started. Attempting to connect...");
                 // --- MODIFICATION: Enable Wi-Fi Power Save Mode ---
                 ESP_LOGI(TAG, "Enabling Wi-Fi Power Save Mode.");
                 esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                 // ---
                 esp_wifi_connect();
                 break;
             case WIFI_EVENT_STA_CONNECTED:
                 ESP_LOGI(TAG, "EVENT: Wi-Fi connected to AP (SSID: %s)", ((wifi_event_sta_connected_t*)event_data)->ssid);
                 led_manager_set_state(LED_STATE_WIFI_CONNECTING);
                 break;
             case WIFI_EVENT_STA_DISCONNECTED: {
                 wifi_event_sta_disconnected_t* evt = (wifi_event_sta_disconnected_t*) event_data;
                 ESP_LOGW(TAG, "EVENT: Wi-Fi disconnected. Reason: %d.", evt->reason);
                 led_manager_set_state(LED_STATE_WIFI_CONNECTING);
                 xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
                 
                 // NEW: Only attempt reconnect if we didn't manually shut down the radio!
                 if (wifi_logging_enabled) {
                     esp_wifi_connect();
                 }
                 break;
             }
             #ifdef CONFIG_PROV_TRANSPORT_SOFTAP
             case WIFI_EVENT_AP_START:
                 ESP_LOGI(TAG, "EVENT: SoftAP Started");
                 led_manager_set_state(LED_STATE_PROVISIONING_ACTIVE);
                 break;
             case WIFI_EVENT_AP_STACONNECTED: {
                 wifi_event_ap_staconnected_t* evt = (wifi_event_ap_staconnected_t*) event_data;
                 ESP_LOGI(TAG, "EVENT: SoftAP: Client "MACSTR" Connected! (AID: %d)", MAC2STR(evt->mac), evt->aid);
                 led_manager_set_state(LED_STATE_PROVISIONING_ACTIVE);
                 break;
             }
             case WIFI_EVENT_AP_STADISCONNECTED: {
                 wifi_event_ap_stadisconnected_t* evt = (wifi_event_ap_stadisconnected_t*) event_data;
                 ESP_LOGI(TAG, "EVENT: SoftAP: Client "MACSTR" Disconnected! (AID: %d)", MAC2STR(evt->mac), evt->aid);
                  led_manager_set_state(LED_STATE_PROVISIONING_ACTIVE);
                 break;
             }
             #endif // CONFIG_PROV_TRANSPORT_SOFTAP
             default:
                 break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
         ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
         ESP_LOGI(TAG, "EVENT: Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
         led_manager_set_state(LED_STATE_WIFI_CONNECTED_IDLE);
         xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
         xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
         #ifdef CONFIG_RESET_PROV_MGR_ON_FAILURE
         retries = 0;
         #endif
    }
    #ifdef CONFIG_PROV_TRANSPORT_BLE
    else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
            case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
                ESP_LOGI(TAG, "EVENT: BLE transport: Connected!");
                led_manager_set_state(LED_STATE_PROVISIONING_ACTIVE);
                break;
            case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
                ESP_LOGI(TAG, "EVENT: BLE transport: Disconnected!");
                led_manager_set_state(LED_STATE_PROVISIONING_ACTIVE);
                break;
            default:
                break;
        }
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
            case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
                ESP_LOGI(TAG, "EVENT: Secured session established!");
                break;
            case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
                ESP_LOGE(TAG, "EVENT: Invalid security parameters!");
                led_manager_set_state(LED_STATE_ERROR);
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT); // Signal failure
                break;
            case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
                ESP_LOGE(TAG, "EVENT: Incorrect username/PoP!");
                led_manager_set_state(LED_STATE_ERROR);
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT); // Signal failure
                break;
            default:
                break;
        }
    }
    #endif // CONFIG_PROV_TRANSPORT_BLE
}