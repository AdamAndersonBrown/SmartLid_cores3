#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

void power_manager_s3_pre_init(void);
void power_manager_s3_init(void);
void cores3_set_screen_power(bool enable);

#ifdef __cplusplus
}
#endif
