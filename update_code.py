import os
import re

def patch_imu_logging_fix():
    filepath = os.path.join("main", "hardware", "imu_telemetry_task.c")
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found.")
        return

    with open(filepath, "r") as f:
        content = f.read()

    # Target the broken struct access in the log statement
    pattern = re.compile(r'sample\.gyro_x,\s*sample\.gyro_y,\s*sample\.gyro_z')
    
    if pattern.search(content):
        # Replace with the local variables and the sleep condition directly
        new_logic = "imu_is_awake ? gyro_x : 0, imu_is_awake ? gyro_y : 0, imu_is_awake ? gyro_z : 0"
        new_content = pattern.sub(new_logic, content)
        
        with open(filepath, "w") as f:
            f.write(new_content)
        print(f"Successfully repaired IMU logging syntax in {filepath}")
    else:
        print(f"Could not find the target log statement in {filepath}. Check if it was modified.")

if __name__ == "__main__":
    print("Initiating surgical patch sequence...")
    patch_imu_logging_fix()
    print("Patch complete.")