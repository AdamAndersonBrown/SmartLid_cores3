# update_autozero.py
import os
import json
import re

def inject_autozero():
    dashboard_dir = r"C:\Workbench\smart_trash_dashboard"
    cal_file = os.path.join(dashboard_dir, "imu_calibration.json")
    
    # 1. Locate C++ Inference Manager dynamically
    cpp_file = None
    for root, dirs, files in os.walk("."):
        if "inference_manager.cpp" in files:
            cpp_file = os.path.join(root, "inference_manager.cpp")
            break
            
    if not cpp_file:
        print("Error: inference_manager.cpp not found.")
        return

    # 2. Extract live floats from JSON to use as the boot-up baseline
    gb = [0.0, 0.0, 0.0]
    if os.path.exists(cal_file):
        with open(cal_file, 'r') as f:
            cal = json.load(f)
            gb = cal.get('gyro_bias_adc', gb)

    # 3. Format the dynamic Auto-Zeroing State Machine
    new_block = f"""// 2. Gyro ZRO (Dynamic Thermal Auto-Zeroing State Machine)
    static float current_gyro_bias[3] = {{{gb[0]:.2f}f, {gb[1]:.2f}f, {gb[2]:.2f}f}};
    static int stationary_count = 0;
    static int32_t gyro_sum[3] = {{0, 0, 0}};

    // Gate: Are we perfectly still? (Raw fluctuations < ~0.4 deg/s)
    if (fabs(gx - current_gyro_bias[0]) < 50.0f &&
        fabs(gy - current_gyro_bias[1]) < 50.0f &&
        fabs(gz - current_gyro_bias[2]) < 50.0f) {{

        gyro_sum[0] += gx; 
        gyro_sum[1] += gy; 
        gyro_sum[2] += gz;
        stationary_count++;

        // If dead silent for 2 seconds (100 frames @ 50Hz), safely recalibrate
        if (stationary_count >= 100) {{
            // Exponential Moving Average (EMA) to prevent quaternion snap (80/20 blend)
            current_gyro_bias[0] = (current_gyro_bias[0] * 0.8f) + (((float)gyro_sum[0] / 100.0f) * 0.2f);
            current_gyro_bias[1] = (current_gyro_bias[1] * 0.8f) + (((float)gyro_sum[1] / 100.0f) * 0.2f);
            current_gyro_bias[2] = (current_gyro_bias[2] * 0.8f) + (((float)gyro_sum[2] / 100.0f) * 0.2f);
            
            stationary_count = 0;
            gyro_sum[0] = 0; gyro_sum[1] = 0; gyro_sum[2] = 0;
        }}
    }} else {{
        // Motion detected: Abort zeroing to protect the baseline
        stationary_count = 0;
        gyro_sum[0] = 0; gyro_sum[1] = 0; gyro_sum[2] = 0;
    }}

    float gx_rad = ((gx - current_gyro_bias[0]) / 131.0f) * 0.0174533f;
    float gy_rad = ((gy - current_gyro_bias[1]) / 131.0f) * 0.0174533f;
    float gz_rad = ((gz - current_gyro_bias[2]) / 131.0f) * 0.0174533f;"""

    # 4. Inject into C++ file
    with open(cpp_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # Find the old static gyro block and replace it
    pattern = r"// 2\. Gyro ZRO.*?float gz_rad = .*?;"
    
    if re.search(pattern, content, flags=re.DOTALL):
        updated_content = re.sub(pattern, new_block, content, flags=re.DOTALL)
        with open(cpp_file, 'w', encoding='utf-8') as f:
            f.write(updated_content)
        print(f"SUCCESS: {cpp_file} patched with Dynamic Thermal Auto-Zeroing.")
    else:
        print("Error: Could not find the Gyro ZRO injection block in C++ file.")

if __name__ == "__main__":
    inject_autozero()