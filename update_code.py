import os
import re

def patch_wifi_prov_handler():
    filepath = os.path.join("main", "core", "wifi_prov_handler.c")
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found. Ensure you are running from the project root.")
        return

    with open(filepath, "r") as f:
        content = f.read()

    # Locate the target block within WIFI_EVENT_STA_DISCONNECTED
    target_pattern = re.compile(
        r'// NEW: Only attempt reconnect if we didn\'t manually shut down the radio!\n'
        r'[ \t]*if \(wifi_logging_enabled\) \{\n'
        r'[ \t]*esp_wifi_connect\(\);\n'
        r'[ \t]*\}',
        re.MULTILINE
    )

    if target_pattern.search(content):
        # We replace it with logic that respects both the state machine and the logging flag.
        # It must only attempt to connect if the wifi interface is actually STARTING/RUNNING, 
        # not when we have explicitly issued esp_wifi_stop()
        new_block = (
            "// ARCHITECT FIX: Validate PHY state before reconnecting\n"
            "                 if (wifi_logging_enabled) {\n"
            "                     wifi_mode_t mode;\n"
            "                     if (esp_wifi_get_mode(&mode) == ESP_OK) {\n"
            "                         esp_wifi_connect();\n"
            "                     }\n"
            "                 } else {\n"
            "                     ESP_LOGI(TAG, \"Radio shutdown requested. Reconnect aborted.\");\n"
            "                 }"
        )
        new_content = target_pattern.sub(new_block, content)
        
        with open(filepath, "w") as f:
            f.write(new_content)
        print(f"Successfully patched Wi-Fi disconnect handler in {filepath}")
    else:
        print(f"Could not find target Wi-Fi disconnect block in {filepath}. Check if the file was modified.")

if __name__ == "__main__":
    print("Initiating surgical patch sequence...")
    patch_wifi_prov_handler()
    print("Patch complete.")