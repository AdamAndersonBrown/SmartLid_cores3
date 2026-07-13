import os, json, re, sys
import numpy as np
import pandas as pd
import tensorflow as tf
from tensorflow.keras import layers, models
from sklearn.model_selection import train_test_split
from sklearn.utils.class_weight import compute_class_weight

DASHBOARD_DIR = r"C:\Workbench\smart_trash_dashboard"
DATA_DIR = os.path.join(DASHBOARD_DIR, "training_data")
WINDOW_SIZE = 100 
NUM_CLASSES = 3

print("--- SmartLid 1D CNN Training Pipeline (V11: Pure Mathematical Parity) ---")

def run_mahony_physics(burst_data):
    # Dynamic Bias: Auto-calibrate using the first 25 frames of the burst
    bias_x = np.mean([p['gx'] for p in burst_data[:25]])
    bias_y = np.mean([p['gy'] for p in burst_data[:25]])
    bias_z = np.mean([p['gz'] for p in burst_data[:25]])

    q = np.array([1.0, 0.0, 0.0, 0.0])
    e_int = np.array([0.0, 0.0, 0.0])
    velocity = np.array([0.0, 0.0, 0.0])
    dt = 0.02; kp = 2.0; ki = 0.005
    
    # --- ESP32 Boot Calibration Simulator ---
    # Give the offline Mahony filter 100 frames to lock onto the true 
    # gravity vector before feature extraction begins.
    first = burst_data[0]
    ax_cal = (first['ax'] / 16384.0) + 0.0300
    ay_cal = (first['ay'] / 16384.0) - 0.0041
    az_cal = (first['az'] / 16384.0) + 0.0692
    
    for _ in range(100):
        norm = np.sqrt(ax_cal**2 + ay_cal**2 + az_cal**2)
        if norm > 0:
            ax_n = ax_cal / norm; ay_n = ay_cal / norm; az_n = az_cal / norm
            vx = 2.0 * (q[1] * q[3] - q[0] * q[2])
            vy = 2.0 * (q[0] * q[1] + q[2] * q[3])
            vz = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3]
            ex = (ay_n * vz - az_n * vy); ey = (az_n * vx - ax_n * vz); ez = (ax_n * vy - ay_n * vx)
            e_int[0] += ex * dt; e_int[1] += ey * dt; e_int[2] += ez * dt
            
            # Standard Mahony update assuming zero gyro movement (static calibration)
            gx = kp * ex + ki * e_int[0]
            gy = kp * ey + ki * e_int[1]
            gz = kp * ez + ki * e_int[2]
            
            qDot = np.array([
                0.5 * (-q[1]*gx - q[2]*gy - q[3]*gz),
                0.5 * ( q[0]*gx + q[2]*gz - q[3]*gy),
                0.5 * ( q[0]*gy - q[1]*gz + q[3]*gx),
                0.5 * ( q[0]*gz + q[1]*gy - q[2]*gx)
            ])
            q += qDot * dt
            q /= np.linalg.norm(q)
            
            a_body = np.array([ax_cal * 9.81, ay_cal * 9.81, az_cal * 9.81])
            q0, q1, q2, q3 = q
            a_earth_x = a_body[0]*(1 - 2*q2*q2 - 2*q3*q3) + a_body[1]*(2*q1*q2 - 2*q0*q3) + a_body[2]*(2*q0*q2 + 2*q1*q3)
            a_earth_y = a_body[0]*(2*q1*q2 + 2*q0*q3) + a_body[1]*(1 - 2*q1*q1 - 2*q3*q3) + a_body[2]*(2*q2*q3 - 2*q0*q1)
            a_earth_z = a_body[0]*(2*q1*q3 - 2*q0*q2) + a_body[1]*(2*q0*q1 + 2*q2*q3) + a_body[2]*(1 - 2*q1*q1 - 2*q2*q2)

            velocity[0] = (velocity[0] + a_earth_x * dt) * 0.92
            velocity[1] = (velocity[1] + a_earth_y * dt) * 0.92
            velocity[2] = (velocity[2] + (a_earth_z - 9.81) * dt) * 0.92
            
    out_features = []
    
    for pt in burst_data:
        # 1. Hardware Matrix
        ax = (pt['ax'] / 16384.0) + 0.0300
        ay = (pt['ay'] / 16384.0) - 0.0041
        az = (pt['az'] / 16384.0) + 0.0692
        c_ax = ax; c_ay = ay; c_az = az

        gx_rad = ((pt['gx'] - bias_x) / 131.0) * 0.0174533
        gy_rad = ((pt['gy'] - bias_y) / 131.0) * 0.0174533
        gz_rad = ((pt['gz'] - bias_z) / 131.0) * 0.0174533

        # 2. Mahony Filter
        norm = np.sqrt(c_ax**2 + c_ay**2 + c_az**2)
        if norm > 0:
            ax_n = c_ax / norm; ay_n = c_ay / norm; az_n = c_az / norm
            vx = 2.0 * (q[1] * q[3] - q[0] * q[2])
            vy = 2.0 * (q[0] * q[1] + q[2] * q[3])
            vz = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3]
            ex = (ay_n * vz - az_n * vy); ey = (az_n * vx - ax_n * vz); ez = (ax_n * vy - ay_n * vx)
            e_int[0] += ex * dt; e_int[1] += ey * dt; e_int[2] += ez * dt
            gx_rad += kp * ex + ki * e_int[0]; gy_rad += kp * ey + ki * e_int[1]; gz_rad += kp * ez + ki * e_int[2]

        qDot = np.array([
            0.5 * (-q[1]*gx_rad - q[2]*gy_rad - q[3]*gz_rad),
            0.5 * ( q[0]*gx_rad + q[2]*gz_rad - q[3]*gy_rad),
            0.5 * ( q[0]*gy_rad - q[1]*gz_rad + q[3]*gx_rad),
            0.5 * ( q[0]*gz_rad + q[1]*gy_rad - q[2]*gx_rad)
        ])
        q += qDot * dt
        q /= np.linalg.norm(q)

        # 3. Leaky Integrator
        a_body = np.array([c_ax * 9.81, c_ay * 9.81, c_az * 9.81])
        q0, q1, q2, q3 = q
        a_earth_x = a_body[0]*(1 - 2*q2*q2 - 2*q3*q3) + a_body[1]*(2*q1*q2 - 2*q0*q3) + a_body[2]*(2*q0*q2 + 2*q1*q3)
        a_earth_y = a_body[0]*(2*q1*q2 + 2*q0*q3) + a_body[1]*(1 - 2*q1*q1 - 2*q3*q3) + a_body[2]*(2*q2*q3 - 2*q0*q1)
        a_earth_z = a_body[0]*(2*q1*q3 - 2*q0*q2) + a_body[1]*(2*q0*q1 + 2*q2*q3) + a_body[2]*(1 - 2*q1*q1 - 2*q2*q2)

        velocity[0] = (velocity[0] + a_earth_x * dt) * 0.92
        velocity[1] = (velocity[1] + a_earth_y * dt) * 0.92
        velocity[2] = (velocity[2] + (a_earth_z - 9.81) * dt) * 0.92

        out_features.append({
            'ts': pt['ts'], 'ignore': pt.get('ignore', False),
            'features': [vx, vy, vz, a_body[0]/20.0, a_body[1]/20.0, a_body[2]/20.0, velocity[0]/2.0, velocity[1]/2.0, velocity[2]/2.0]
        })
    return out_features

def load_and_window_data(directory):
    print("Parsing files to extract bursts...")
    bursts_by_tag = {0: [], 1: [], 2: []}
    
    # PASS 1: Extract and compute Mahony physics for all bursts
    for filename in os.listdir(directory):
        if not filename.endswith(".jsonl"): continue
        import re
        match = re.search(r'class_(\d+)', filename)
        if not match: continue
        true_tag = int(match.group(1))
        
        # Explicitly ignore Class 3 (Tumbling) per architecture bounds
        if true_tag >= NUM_CLASSES: continue
        
        filepath = os.path.join(directory, filename)
        raw_data = []
        import json
        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                try:
                    parsed = json.loads(line.strip())
                    pts = parsed if isinstance(parsed, list) else [parsed]
                    raw_data.extend(pts)
                except: pass
                
        if not raw_data: continue
        
        # Split into initial bursts (>1 second gaps)
        initial_bursts = []
        current = []
        for i, pt in enumerate(raw_data):
            if i == 0:
                current.append(pt); continue
            if pt['ts'] - raw_data[i-1]['ts'] > 1000000:
                if len(current) > 50: initial_bursts.append(current)
                current = []
            current.append(pt)
        if len(current) > 50: initial_bursts.append(current)

        import numpy as np
        for burst in initial_bursts:
            phys_data = run_mahony_physics(burst)
            clean_data = [p['features'] for p in phys_data if not p['ignore']]
            if len(clean_data) < WINDOW_SIZE: continue
            
            features_array = np.array(clean_data)
            
            # Artificial Event Limitation: Slice Idle/Rattle into 5-second (250 frame) chunks
            if true_tag in [0, 1]:
                chunk_size = 250
                for i in range(0, len(features_array), chunk_size):
                    chunk = features_array[i:i + chunk_size]
                    if len(chunk) >= WINDOW_SIZE:
                        bursts_by_tag[true_tag].append(chunk)
            else:
                # Lifts (Class 2) remain as full continuous sequences
                bursts_by_tag[true_tag].append(features_array)
            
    # PASS 2: Infer Noise Power strictly from Class 0 (Idle)
    print("Inferring noise power from Class 0 (Idle)...")
    if not bursts_by_tag[0]:
        raise ValueError("No Class 0 data found to calculate noise baseline.")
        
    all_idle_stds = [np.std(burst, axis=0) for burst in bursts_by_tag[0]]
    noise_std = np.mean(all_idle_stds, axis=0)

    X_all, y_all = [], []
    
    print("Padding events and extracting sliding windows...")
    for tag, bursts in bursts_by_tag.items():
        for clean_data in bursts:
            first_frame = np.copy(clean_data[0])
            first_frame[6:9] = 0.0  
            last_frame = np.copy(clean_data[-1])
            last_frame[6:9] = 0.0
            
            pre_pad = np.tile(first_frame, (WINDOW_SIZE, 1)) + np.random.normal(0, noise_std, (WINDOW_SIZE, 9))
            post_pad = np.tile(last_frame, (WINDOW_SIZE, 1)) + np.random.normal(0, noise_std, (WINDOW_SIZE, 9))
            
            continuous_features = np.vstack([pre_pad, clean_data, post_pad])
            labels = np.concatenate([
                np.zeros(WINDOW_SIZE), 
                np.full(len(clean_data), tag), 
                np.zeros(WINDOW_SIZE)
            ])
            
            step_size = WINDOW_SIZE // 2
            for i in range(0, len(continuous_features) - WINDOW_SIZE, step_size):
                window = continuous_features[i : i + WINDOW_SIZE]
                window_label = int(labels[i + WINDOW_SIZE - 1])
                
                if window_label != 0:
                    dyn_vel_mag = np.sqrt(window[:, 6]**2 + window[:, 7]**2 + window[:, 8]**2)
                    if np.max(dyn_vel_mag) < 0.05: continue
                    
                X_all.append(window)
                y_all.append(window_label)
                
    return np.array(X_all), np.array(y_all)


X, y = load_and_window_data(DATA_DIR)
if len(X) == 0: exit("ERROR: No valid data found!")

X_train, X_val, y_train, y_val = train_test_split(X, y, test_size=0.2, random_state=42)
class_weight_dict = dict(enumerate(compute_class_weight('balanced', classes=np.unique(y_train), y=y_train)))

model = models.Sequential([
    layers.Input(shape=(WINDOW_SIZE, 9)),
    layers.Conv1D(filters=16, kernel_size=3, activation='relu'),
    layers.MaxPooling1D(pool_size=2),
    layers.Conv1D(filters=32, kernel_size=3, activation='relu'),
    layers.MaxPooling1D(pool_size=2),
    layers.Flatten(),
    layers.Dense(32, activation='relu'),
    layers.Dropout(0.3),
    layers.Dense(NUM_CLASSES, activation='softmax') 
])

model.compile(optimizer='adam', loss='sparse_categorical_crossentropy', metrics=['accuracy'])
model.fit(X_train, y_train, epochs=40, validation_data=(X_val, y_val), batch_size=32, class_weight=class_weight_dict)

converter = tf.lite.TFLiteConverter.from_keras_model(model)
tflite_model = converter.convert()

output_path = os.path.join("main", "ml", "model_data.h")
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w") as f:
    f.write("// Auto-generated Model V11\n#ifndef MODEL_DATA_H\n#define MODEL_DATA_H\n\n")
    f.write(f"const unsigned int smartlid_model_tflite_len = {len(tflite_model)};\n")
    f.write("const unsigned char smartlid_model_tflite[] = {\n")
    hex_array = [f"0x{b:02x}" for b in tflite_model]
    for i in range(0, len(hex_array), 12): f.write("    " + ", ".join(hex_array[i:i+12]) + ",\n")
    f.write("};\n\n#endif\n")

print("\n--- Validating Exported TFLite Model ---")
interpreter = tf.lite.Interpreter(model_content=tflite_model)
interpreter.allocate_tensors()
in_idx = interpreter.get_input_details()[0]['index']
out_idx = interpreter.get_output_details()[0]['index']

correct = 0
y_pred_val = []
for i in range(len(X_val)):
    interpreter.set_tensor(in_idx, np.expand_dims(X_val[i], axis=0).astype(np.float32))
    interpreter.invoke()
    pred = np.argmax(interpreter.get_tensor(out_idx))
    y_pred_val.append(pred)
    if pred == y_val[i]: correct += 1

print(f"TFLite Validation Accuracy: {correct / len(X_val) * 100:.2f}%")
try:
    from sklearn.metrics import confusion_matrix
    print("Confusion Matrix (Rows=True, Cols=Predicted):\n", confusion_matrix(y_val, y_pred_val))
except ImportError:
    pass

print(f"\nSUCCESS! Model exported automatically to: {output_path}")

