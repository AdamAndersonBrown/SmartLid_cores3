# audit_data.py
import os
import sys
import json
import numpy as np
import matplotlib.pyplot as plt

DASHBOARD_DIR = r"C:\Workbench\smart_trash_dashboard"
sys.path.append(DASHBOARD_DIR)
import imu_filter

def crawl_and_plot():
    lift_file = os.path.join(DASHBOARD_DIR, "training_data", "class_2_lift.jsonl")
    if not os.path.exists(lift_file):
        print(f"Error: Could not find {lift_file}")
        return

    data = []
    with open(lift_file, 'r', encoding='utf-8') as f:
        for line in f:
            try:
                parsed = json.loads(line.strip())
                pts = parsed if isinstance(parsed, list) else [parsed]
                for pt in pts:
                    if not pt.get('ignore', False):
                        data.append(pt)
            except: pass

    # Run the raw data through your exact ESP32 Mahony simulation
    old_cwd = os.getcwd()
    os.chdir(DASHBOARD_DIR)
    engine = imu_filter.IMUFusionEngine(sample_rate=50.0)
    motion_path = engine.process_window(data)
    os.chdir(old_cwd)

    # Group into continuous bursts (assuming >1 sec gap means a new physical recording burst)
    bursts = []
    current_burst = []
    for i, pt in enumerate(motion_path):
        if i == 0:
            current_burst.append(pt)
            continue
        
        # If timestamp jumps by more than 1 second (1,000,000 us), split the burst
        if pt['ts'] - motion_path[i-1]['ts'] > 1000000:
            if len(current_burst) > 25: bursts.append(current_burst)
            current_burst = []
        current_burst.append(pt)
    if len(current_burst) > 25: bursts.append(current_burst)

    plt.figure(figsize=(12, 6))
    plt.title("Z-Velocity Crawler: Isolating True Lifts vs. Set-Downs", fontsize=14, color='white')
    
    print("--- DATA CRAWL REPORT ---")
    suspect_count = 0
    
    for idx, burst in enumerate(bursts):
        vz_array = np.array([pt['vz'] for pt in burst])
        ts_start = burst[0]['ts']
        
        # Find the absolute peak velocity in this burst
        max_up = np.max(vz_array)
        max_down = np.min(vz_array)
        
        # If the downward velocity is stronger than the upward velocity, it's a Set-Down!
        if abs(max_down) > max_up:
            color = '#f85149' # Red for Suspect Set-Down
            label = "Set-Down (Suspect)" if suspect_count == 0 else ""
            print(f"[!] SUSPECT SET-DOWN FOUND: Burst starts at TS: {ts_start}. Max Up: {max_up:.2f}, Max Down: {max_down:.2f}")
            suspect_count += 1
        else:
            color = '#3fb950' # Green for True Lift
            label = "True Lift" if idx == 0 else ""
            
        plt.plot(vz_array, color=color, alpha=0.7, label=label, linewidth=2)

    print(f"\nTotal Lift Bursts Analyzed: {len(bursts)}")
    print(f"Total Suspect Set-Downs Flagged: {suspect_count}")
    print("Close the plot window to finish.")

    # Styling the plot to match your dashboard
    ax = plt.gca()
    ax.set_facecolor('#0d1117')
    plt.gcf().patch.set_facecolor('#0d1117')
    ax.spines['bottom'].set_color('#30363d')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.spines['left'].set_color('#30363d')
    ax.tick_params(colors='#8b949e')
    plt.axhline(0, color='#8b949e', linestyle='--', linewidth=1)
    plt.ylabel('Earth Z-Velocity (m/s)', color='#c9d1d9')
    plt.xlabel('Time Steps (20ms)', color='#c9d1d9')
    plt.legend(facecolor='#21262d', edgecolor='#30363d', labelcolor='white')
    plt.show()

if __name__ == "__main__":
    crawl_and_plot()