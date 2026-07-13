#ifndef INFERENCE_MANAGER_H
#define INFERENCE_MANAGER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void inference_manager_init(void);
void inference_push_data(int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz);
void inference_run(void);
void inference_task(void *pvParameters);
#ifdef __cplusplus
}
#endif
#endif
