#ifndef WIFI_PROV_HANDLER_H
#define WIFI_PROV_HANDLER_H

#include "esp_err.h"
#include "esp_event.h"
#include "wifi_provisioning/manager.h" // For wifi_prov_security_t
#include <stddef.h> // For size_t
#include <stdint.h> // For uint8_t, uint16_t, uint32_t, ssize_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Unified event handler for Wi-Fi, IP, and Provisioning events.
 *
 * @param arg User argument (not used).
 * @param event_base The base type of the event.
 * @param event_id The specific event ID.
 * @param event_data Data associated with the event.
 */
void event_handler(void* arg, esp_event_base_t event_base,
                   int32_t event_id, void* event_data);

/**
 * @brief Generates a unique service name for provisioning based on MAC address.
 *
 * @param service_name Buffer to store the generated name.
 * @param max Maximum size of the service_name buffer.
 */
void get_device_service_name(char *service_name, size_t max);

/**
 * @brief Handler for custom provisioning data endpoint (example).
 *
 * @param session_id Session ID.
 * @param inbuf Input data buffer.
 * @param inlen Length of input data.
 * @param outbuf Pointer to output data buffer pointer.
 * @param outlen Pointer to output data length.
 * @param priv_data Private data pointer.
 * @return esp_err_t Error code.
 */
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data);


// --- Security Version 2 Helper Declarations ---
// Use renamed Kconfig options
#ifdef CONFIG_PROV_SECURITY_VERSION_2
/**
 * @brief Get the salt for Security Version 2. Implementation depends on Kconfig mode.
 *
 * @param salt Pointer to store the salt data pointer.
 * @param salt_len Pointer to store the salt length.
 * @return esp_err_t Error code.
 */
esp_err_t example_get_sec2_salt(const char **salt, uint16_t *salt_len);

/**
 * @brief Get the verifier for Security Version 2. Implementation depends on Kconfig mode.
 *
 * @param verifier Pointer to store the verifier data pointer.
 * @param verifier_len Pointer to store the verifier length.
 * @return esp_err_t Error code.
 */
esp_err_t example_get_sec2_verifier(const char **verifier, uint16_t *verifier_len);
#endif // CONFIG_PROV_SECURITY_VERSION_2


// --- QR Code Helper Declaration ---
// Use renamed Kconfig options
#ifdef CONFIG_PROV_SHOW_QR
/**
 * @brief Generates and prints a QR code for provisioning to the console logs.
 *
 * @param name Service name of the device.
 * @param username Username (only used for Sec2 dev mode).
 * @param pop Proof-of-Possession string (used for Sec1 and Sec2 dev mode).
 * @param transport Transport type ("softap" or "ble").
 */
void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport);
#endif // CONFIG_PROV_SHOW_QR


#ifdef __cplusplus
}
#endif

#endif // WIFI_PROV_HANDLER_H
