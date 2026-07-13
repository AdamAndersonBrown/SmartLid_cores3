# update_code.py
import os
import re

def patch_file():
    filepath = os.path.join("main", "hardware", "imu_telemetry_task.c")
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found.")
        return
        
    with open(filepath, "r") as file:
        content = file.read()

    # Stretch the 20ms delay to 1000ms to force FreeRTOS into Light Sleep
    old_macro = "#define STREAM_DELAY_MS 20"
    new_macro = "#define STREAM_DELAY_MS 1000 // HACK: Force Light Sleep for Battery Test"

    if new_macro in content:
        print("Sleep thrashing patch already applied.")
    elif old_macro in content:
        updated = content.replace(old_macro, new_macro)
        with open(filepath, "w") as file:
            file.write(updated)
        print("Successfully stretched the IMU polling window to 1000ms.")
    else:
        print("Error: Target macro not found.")

if __name__ == "__main__":
    patch_file()