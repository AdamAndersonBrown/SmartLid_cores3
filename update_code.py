import os
import re

def resolve_hardware_collisions():
    # 1. Patch Speaker Manager (Resolve GPIO 0 / GPIO 12 I2C Deadlocks)
    paths_speaker = [
        os.path.join("main", "hardware", "speaker_manager.cpp"),
        os.path.join("main", "hardware", "speaker_manager.c"),
        os.path.join("main", "speaker_manager.cpp"),
        os.path.join("main", "speaker_manager.c")
    ]
    for path in paths_speaker:
        if os.path.exists(path):
            with open(path, "r") as f:
                content = f.read()
            
            # Map to correct CoreS3 I2S pins
            content = re.sub(r'\.bck_io_num\s*=\s*\d+', '.bck_io_num = 34', content)
            content = re.sub(r'\.ws_io_num\s*=\s*\d+', '.ws_io_num = 33', content)
            content = re.sub(r'\.data_out_num\s*=\s*\d+', '.data_out_num = 13', content)
            content = re.sub(r'\.data_in_num\s*=\s*\d+', '.data_in_num = 14', content)
            
            # Remove legacy Core2 GPIO 2 amplifier enable logic
            content = re.sub(r'gpio_set_direction\(\s*(GPIO_NUM_2|2)\s*,\s*GPIO_MODE_OUTPUT\s*\);[^\n]*\n', '', content)
            content = re.sub(r'gpio_set_level\(\s*(GPIO_NUM_2|2)\s*,\s*\d+\s*\);[^\n]*\n', '', content)
            
            with open(path, "w") as f:
                f.write(content)
            print(f"SUCCESS: Patched {path} (CoreS3 I2S pinout applied; Green LED & I2C bus secured)")
            break
    else:
        print("ERROR: speaker_manager file not found.")

    # 2. Patch Servo Manager (Move off internal audio bus to Port B)
    paths_servo = [
        os.path.join("main", "hardware", "servo_manager.cpp"),
        os.path.join("main", "hardware", "servo_manager.c"),
        os.path.join("main", "servo_manager.cpp"),
        os.path.join("main", "servo_manager.c")
    ]
    for path in paths_servo:
        if os.path.exists(path):
            with open(path, "r") as f:
                content = f.read()
            
            content = re.sub(r'\.gen_gpio_num\s*=\s*\d+', '.gen_gpio_num = 8', content)
            content = re.sub(r'#define\s+SERVO_PIN\s+\d+', '#define SERVO_PIN 8', content)
            
            with open(path, "w") as f:
                f.write(content)
            print(f"SUCCESS: Patched {path} (Moved servo to GPIO 8)")
            break
    else:
        print("ERROR: servo_manager file not found.")

    # 3. Patch App Main (Disable Light Sleep)
    paths_main = [
        os.path.join("main", "core", "app_main.c"),
        os.path.join("main", "main.cpp")
    ]
    for path in paths_main:
        if os.path.exists(path):
            with open(path, "r") as f:
                content = f.read()
            
            content = re.sub(r'\.light_sleep_enable\s*=\s*true', '.light_sleep_enable = false // CRITICAL: Must be FALSE to keep the USB COM port alive!', content)
            
            with open(path, "w") as f:
                f.write(content)
            print(f"SUCCESS: Patched {path} (Disabled Light Sleep for USB debugging)")
            break
    else:
        print("ERROR: app_main file not found.")

    # 4. Patch Inference Manager (Uncomment Telemetry)
    paths_inf = [
        os.path.join("main", "ml", "inference_manager.cpp"),
        os.path.join("main", "inference_manager.cpp")
    ]
    for path in paths_inf:
        if os.path.exists(path):
            with open(path, "r") as f:
                content = f.read()
            
            # Target the commented out ESP_LOGI statements and cleanly expose them
            content = re.sub(r'//\s*(ESP_LOGI\("ML_TELEMETRY")', r'\1', content)
            content = re.sub(r'//\s*(results\[0\]\s*\*\s*100\.0f)', r'\1', content)
            
            with open(path, "w") as f:
                f.write(content)
            print(f"SUCCESS: Patched {path} (Uncommented ML telemetry logs)")
            break
    else:
        print("ERROR: inference_manager file not found.")

if __name__ == "__main__":
    resolve_hardware_collisions()