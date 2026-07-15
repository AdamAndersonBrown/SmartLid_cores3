#include "inference_manager.h"
#include "common_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_data.h"
#include "esp_log.h"
#include <math.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

extern "C" void speaker_play_rattle(void);
extern "C" void display_manager_set_alert(int class_id);
extern "C" void servo_trigger_unlock_sequence(void);

#define WINDOW_SIZE 100
#define ML_FEATURES 9

static float ring_buffer[WINDOW_SIZE][ML_FEATURES];
static int buffer_index = 0;
static bool buffer_full = false;

static float q[4] = {1.0f, 0.0f, 0.0f, 0.0f};
static float e_int[3] = {0.0f, 0.0f, 0.0f};
static float velocity[3] = {0.0f, 0.0f, 0.0f};

// V12: Unconditional Boot Calibration
static bool boot_calibrated = false;
static int boot_frames = 0;
static int32_t boot_sum[3] = {0, 0, 0};
static float gyro_bias[3] = {0.0f, 0.0f, 0.0f};

const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;

constexpr int kTensorArenaSize = 48 * 1024;
__attribute__((section(".dram1.bss"))) alignas(16) static uint8_t tensor_arena[kTensorArenaSize];

extern "C" void inference_manager_init(void) {
    model = tflite::GetModel(smartlid_model_tflite);
    static tflite::MicroMutableOpResolver<15> resolver;
    resolver.AddConv2D(); resolver.AddMaxPool2D(); resolver.AddFullyConnected();
    resolver.AddSoftmax(); resolver.AddReshape(); resolver.AddRelu();
    resolver.AddExpandDims(); resolver.AddSqueeze(); resolver.AddMean();
    resolver.AddQuantize(); resolver.AddShape(); resolver.AddStridedSlice();
    resolver.AddPack(); resolver.AddDequantize(); resolver.AddMul();

    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;
    if (interpreter->AllocateTensors() != kTfLiteOk) { ESP_LOGE("TFLM", "AllocateTensors() failed! Arena too small."); return; }
    input = interpreter->input(0);
    output = interpreter->output(0);
    ESP_LOGI("TFLM", "Edge AI V12 (Unconditional Boot Calibrator) Initialized.");
}

extern "C" void inference_push_data(int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz) {
    // --- 1. UNCONDITIONAL BOOT CALIBRATION ---
    // No noise gate. Just blindly average the first 50 frames to match Python parity.
    if (!boot_calibrated) {
        boot_sum[0] += gx; boot_sum[1] += gy; boot_sum[2] += gz;
        boot_frames++;
        if (boot_frames >= 50) {
            gyro_bias[0] = boot_sum[0] / 50.0f;
            gyro_bias[1] = boot_sum[1] / 50.0f;
            gyro_bias[2] = boot_sum[2] / 50.0f;
            boot_calibrated = true;
            ESP_LOGW("TFLM", "BOOT CALIBRATION COMPLETE: X:%.1f Y:%.1f Z:%.1f", gyro_bias[0], gyro_bias[1], gyro_bias[2]);
        }
        return; // Starve the ML buffer until the bias is mathematically locked
    }

    // --- 2. KINEMATICS & MAHONY ---
    // Re-tuned Mahony gains for BMI270 (Lower kp prevents violent divergence during taps)
    float dt = 0.02f; float kp = 0.5f; float ki = 0.001f;
    // STRICT FIX: Eradicate hardcoded MPU6886 biases (Prevents phantom Mahony tilt)
    float ax_g = (ax / 16384.0f);
    float ay_g = (ay / 16384.0f);
    float az_g = (az / 16384.0f);

    float gx_rad = ((gx - gyro_bias[0]) / 131.0f) * 0.0174533f;
    float gy_rad = ((gy - gyro_bias[1]) / 131.0f) * 0.0174533f;
    float gz_rad = ((gz - gyro_bias[2]) / 131.0f) * 0.0174533f;

    float norm = sqrtf(ax_g*ax_g + ay_g*ay_g + az_g*az_g);
    if (norm > 0.0f) {
        float ax_n = ax_g / norm; float ay_n = ay_g / norm; float az_n = az_g / norm;
        float vx = 2.0f * (q[1] * q[3] - q[0] * q[2]);
        float vy = 2.0f * (q[0] * q[1] + q[2] * q[3]);
        float vz = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];

        float ex = (ay_n * vz - az_n * vy); float ey = (az_n * vx - ax_n * vz); float ez = (ax_n * vy - ay_n * vx);
        e_int[0] += ex * dt; e_int[1] += ey * dt; e_int[2] += ez * dt;
        gx_rad += kp * ex + ki * e_int[0]; gy_rad += kp * ey + ki * e_int[1]; gz_rad += kp * ez + ki * e_int[2];
    }

    float qDot1 = 0.5f * (-q[1]*gx_rad - q[2]*gy_rad - q[3]*gz_rad);
    float qDot2 = 0.5f * (q[0]*gx_rad + q[2]*gz_rad - q[3]*gy_rad);
    float qDot3 = 0.5f * (q[0]*gy_rad - q[1]*gz_rad + q[3]*gx_rad);
    float qDot4 = 0.5f * (q[0]*gz_rad + q[1]*gy_rad - q[2]*gx_rad);
    q[0] += qDot1 * dt; q[1] += qDot2 * dt; q[2] += qDot3 * dt; q[3] += qDot4 * dt;
    float q_norm = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    q[0] /= q_norm; q[1] /= q_norm; q[2] /= q_norm; q[3] /= q_norm;

    float a_body[3] = {ax_g * 9.81f, ay_g * 9.81f, az_g * 9.81f};
    float q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];
    float a_earth_x = a_body[0]*(1 - 2*q2*q2 - 2*q3*q3) + a_body[1]*(2*q1*q2 - 2*q0*q3) + a_body[2]*(2*q0*q2 + 2*q1*q3);
    float a_earth_y = a_body[0]*(2*q1*q2 + 2*q0*q3) + a_body[1]*(1 - 2*q1*q1 - 2*q3*q3) + a_body[2]*(2*q2*q3 - 2*q0*q1);
    float a_earth_z = a_body[0]*(2*q1*q3 - 2*q0*q2) + a_body[1]*(2*q0*q1 + 2*q2*q3) + a_body[2]*(1 - 2*q1*q1 - 2*q2*q2);
    
    velocity[0] = (velocity[0] + a_earth_x * dt) * 0.92f;
    velocity[1] = (velocity[1] + a_earth_y * dt) * 0.92f;
    velocity[2] = (velocity[2] + (a_earth_z - 9.81f) * dt) * 0.92f;

    for (int i = 0; i < WINDOW_SIZE - 1; i++) {
        for (int j = 0; j < ML_FEATURES; j++) { ring_buffer[i][j] = ring_buffer[i+1][j]; }
    }
    // --- INSTANT CONVERGENCE HACK ---
    // Let the Mahony filter run, but block the TFLite ring buffer from recording 
    // until the filter has completely settled onto the true gravity vector.
    static int convergence_frames = 0;
    if (convergence_frames < 150) {
        convergence_frames++;
        if (convergence_frames == 150) {
            velocity[0] = 0.0f; velocity[1] = 0.0f; velocity[2] = 0.0f;
            ESP_LOGW("TFLM", "Filter Convergence Complete. Real-time inference armed.");
        }
        return; // Skip the buffer push and inference engine
    }

    // 100x9 Yaw-Invariant Spatial Architecture
    float vx = 2.0f * (q[1] * q[3] - q[0] * q[2]);
    float vy = 2.0f * (q[0] * q[1] + q[2] * q[3]);
    float vz = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];

    ring_buffer[WINDOW_SIZE - 1][0] = vx; 
    ring_buffer[WINDOW_SIZE - 1][1] = vy;
    ring_buffer[WINDOW_SIZE - 1][2] = vz;
    ring_buffer[WINDOW_SIZE - 1][3] = a_body[0] / 20.0f;
    ring_buffer[WINDOW_SIZE - 1][4] = a_body[1] / 20.0f;
    ring_buffer[WINDOW_SIZE - 1][5] = a_body[2] / 20.0f;
    ring_buffer[WINDOW_SIZE - 1][6] = velocity[0] / 2.0f;
    ring_buffer[WINDOW_SIZE - 1][7] = velocity[1] / 2.0f;
    ring_buffer[WINDOW_SIZE - 1][8] = velocity[2] / 2.0f;

    if (buffer_index < WINDOW_SIZE) {
        buffer_index++;
        if (buffer_index == WINDOW_SIZE) buffer_full = true;
    }
}

extern "C" void inference_run(void) {
    if (!buffer_full || !interpreter || !input || !output) return;
    float* input_data = input->data.f;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        for (int j = 0; j < ML_FEATURES; j++) { input_data[i * ML_FEATURES + j] = ring_buffer[i][j]; }
    }
    if (interpreter->Invoke() != kTfLiteOk) return;

    float* results = output->data.f;
    int max_class = 0; float max_prob = results[0];
    for (int i = 1; i < 3; i++) {
        if (results[i] > max_prob) { max_prob = results[i]; max_class = i; }
    }

    if (1) { // Unconditional logging unlocked
        ESP_LOGI("ML_TELEMETRY", "Idle: %3.0f%% | Rattle: %3.0f%% | Lift: %3.0f%% | VZ: %.2f",
        results[0] * 100.0f, results[1] * 100.0f, results[2] * 100.0f, velocity[2]);
        // ESP_LOGW("MAHONY_DEBUG", "Quat: [%.2f, %.2f, %.2f, %.2f] | Bias-Free Gyro Y: %.1f",
//                  q[0], q[1], q[2], q[3], (ring_buffer[WINDOW_SIZE-1][8] * 2.0f));
    }

    static int current_triggered_class = 0;
    static TickType_t last_trigger_time = 0;
    TickType_t now = xTaskGetTickCount();

    if (max_prob > 0.85f) {
        if (max_class != 0) {
            if ((max_class == current_triggered_class) && ((now - last_trigger_time) < pdMS_TO_TICKS(3000))) return; 
            last_trigger_time = now; 
        }
        if (max_class == 1 || max_class == 2) {
            display_manager_set_alert(max_class);
            if (max_class == 1 && current_triggered_class != 1) speaker_play_rattle();
            if (max_class == 2 && current_triggered_class != 2) servo_trigger_unlock_sequence();
        } else {
            display_manager_set_alert(0); 
        }
        current_triggered_class = max_class;
    }
}

extern "C" void inference_task(void *pvParameters) {
    imu_sample_t sample;
    while(1) {
        if (xQueueReceive(imu_queue, &sample, portMAX_DELAY) == pdTRUE) {
            inference_push_data(sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz);
            while (xQueueReceive(imu_queue, &sample, 0) == pdTRUE) {
                inference_push_data(sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz);
            }
            inference_run();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
