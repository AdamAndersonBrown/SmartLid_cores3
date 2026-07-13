#include "led_manager.h"
#include "esp_log.h"
void led_manager_init(void) { ESP_LOGI("LED_MAN", "Stub Initialized"); }
void led_manager_set_state(led_state_t state) { ESP_LOGD("LED_MAN", "State: %d", state); }
