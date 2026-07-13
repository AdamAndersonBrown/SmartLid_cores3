import os

print("Deploying Edge AI Hooks to SmartLid (Safe Mode)...")

def find_file(filename):
    for root, dirs, files in os.walk("."):
        if filename in files: return os.path.join(root, filename)
    return None

# 1. TFLM Component Dependency
comp_yml = find_file("idf_component.yml") or "main/idf_component.yml"
os.makedirs(os.path.dirname(comp_yml), exist_ok=True)
with open(comp_yml, "w") as f:
    f.write("dependencies:\n  espressif/esp-tflite-micro: \"*\"\n")
print(f"-> {comp_yml} generated (TFLM dependency injected).")

# 2. Patch CMakeLists.txt safely
cmake_path = find_file("CMakeLists.txt")
if cmake_path:
    with open(cmake_path, "r") as f: content = f.read()
    if 'inference_manager.cpp' not in content and 'app_main.c' in content:
        content = content.replace('SRCS "app_main.c"', 'SRCS "app_main.c" "inference_manager.cpp"')
        with open(cmake_path, "w") as f: f.write(content)
        print(f"-> {cmake_path} patched.")

# 3. Patch Display Manager safely
disp_h = find_file("display_manager.h")
if disp_h:
    with open(disp_h, "r") as f: content = f.read()
    if "display_manager_set_alert" not in content:
        content = content.replace('#define COLOR_WHITE', '#define COLOR_GREEN 0x07E0\n#define COLOR_WHITE')
        content = content.replace('void display_manager_draw_tag', 'void display_manager_set_alert(int class_id);\nvoid display_manager_draw_tag')
        with open(disp_h, "w") as f: f.write(content)
        print(f"-> {disp_h} patched.")

disp_c = find_file("display_manager.c")
if disp_c:
    with open(disp_c, "r") as f: content = f.read()
    if "display_manager_set_alert" not in content:
        alert_logic = """
void display_manager_set_alert(int class_id) {
    if (!panel_handle) return;
    static int last_class = -1;
    if (class_id == last_class) return;
    last_class = class_id;
    uint16_t color = (class_id == 2) ? COLOR_GREEN : 0x0000;
    static uint16_t row_buf[320 * 10];
    for (int i = 0; i < 320 * 10; i++) row_buf[i] = color;
    for (int y = 30; y < 210; y += 10) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, 320, y + 10, row_buf);
    }
}
"""
        content += alert_logic
        with open(disp_c, "w") as f: f.write(content)
        print(f"-> {disp_c} patched.")

# 4. Patch app_main.c safely
main_c = find_file("app_main.c")
if main_c:
    with open(main_c, "r") as f: content = f.read()
    if "inference_manager_init();" not in content:
        content = '#include "inference_manager.h"\n' + content
        content = content.replace("speaker_manager_init();", "speaker_manager_init();\n    inference_manager_init();")
        with open(main_c, "w") as f: f.write(content)
        print(f"-> {main_c} patched.")

print("\nEdge ML Deployment Script Complete!")
