// Filename: error_manager.h
#ifndef ERROR_MANAGER_H
#define ERROR_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ERR_CODE_NONE = 0,
    ERR_WIFI_CONNECT_FAIL,
    ERR_OTA_FAIL,
    ERR_I2S_INIT_FAIL,
    ERR_CONFIG_LOAD_FAIL,
    ERR_RINGBUF_FAIL,
    ERR_TIME_SYNC_FAILED,
    ERR_CONFIG_SAVE_FAIL,
    ERR_STREAMING_STALLED,
    ERR_UNKNOWN
} last_error_code_t;

/**
 * @brief Initializes the error manager by loading the last error code into the RAM cache.
 */
void error_manager_init(void);
void error_manager_set_last_error(last_error_code_t error_code);
last_error_code_t error_manager_get_last_error(void);
const char* error_manager_get_string(last_error_code_t error_code);


#ifdef __cplusplus
}
#endif

#endif // ERROR_MANAGER_H