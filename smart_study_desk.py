# ============================================================
#  AI-Enabled Smart Study Desk - Python Script
#  Features: Flask dashboard + webcam/detection control via web
#
#  Install: pip install opencv-python mediapipe ultralytics pyserial flask
# ============================================================

import cv2
import mediapipe as mp
import serial
import time
import threading
from ultralytics import YOLO
from flask import Flask, Response, jsonify, render_template, request
from datetime import datetime

# ============================================================
#  SHARED STATE
# ============================================================

data_store = {
    "temperature"  : 0.0,
    "humidity"     : 0.0,
    "gas"           : 0,
    "light_ao"      : 0,
    "light_dark"    : False,
    "light_status"  : "Good",
    "posture"      : "Good",
    "phone"        : "Not Detected",
    "alert"        : "System Ready",
    "alert_type"   : "good",
    "last_updated" : "",
    "temp_history" : [],
    "hum_history"  : [],
    "gas_history"   : [],
    "light_history" : [],
    "time_history" : [],
    "webcam_on"    : False,
    "detection_on" : False,
    "session_alerts": 0,
    "focus_score"  : 100,
}

# Control flags (toggled by web buttons)
webcam_active    = False
detection_active = False

data_lock  = threading.Lock()
frame_lock = threading.Lock()
latest_frame_bytes = None

# ============================================================
#  FLASK APP
# ============================================================

app = Flask(__name__)

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/data")
def get_data():
    with data_lock:
        return jsonify(data_store)

@app.route("/video_feed")
def video_feed():
    return Response(generate_frames(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")

@app.route("/control", methods=["POST"])
def control():
    """
    Web buttons call this to start/stop webcam or detection.
    Body: { "action": "start_webcam" | "stop_webcam" |
                      "start_detection" | "stop_detection" }
    """
    global webcam_active, detection_active
    body   = request.get_json()
    action = body.get("action", "")

    if action == "start_webcam":
        webcam_active = True
    elif action == "stop_webcam":
        webcam_active    = False
        detection_active = False          # Can't detect without webcam
    elif action == "start_detection":
        if webcam_active:
            detection_active = True
    elif action == "stop_detection":
        detection_active = False

    with data_lock:
        data_store["webcam_on"]    = webcam_active
        data_store["detection_on"] = detection_active

    return jsonify({"status": "ok",
                    "webcam_on": webcam_active,
                    "detection_on": detection_active})

def generate_frames():
    global latest_frame_bytes
    while True:
        with frame_lock:
            frame = latest_frame_bytes
        if frame is not None:
            yield (b"--frame\r\n"
                   b"Content-Type: image/jpeg\r\n\r\n" + frame + b"\r\n")
        time.sleep(0.05)

def run_flask():
    app.run(host="0.0.0.0", port=8080, debug=False, use_reloader=False)

# ============================================================
#  ARDUINO
# ============================================================

try:
    arduino = serial.Serial(port='COM9', baudrate=9600, timeout=1)
    time.sleep(2)
    print("Arduino connected!")
except Exception as e:
    print(f"Arduino not connected: {e}")
    arduino = None

def send_alert(code):
    if arduino and arduino.is_open:
        try:
            arduino.write(code.encode())
        except:
            pass

def get_light_status(ao_val):
    if   ao_val < 100:  return "Excellent"
    elif ao_val < 300:  return "Good"
    elif ao_val < 600:  return "Dim"
    else:               return "Too Dark!"

def update_store(temp, hum, gas, light_ao, light_dark, now):
    light_status = get_light_status(light_ao)
    MAX = 20
    with data_lock:
        data_store["temperature"]  = temp
        data_store["humidity"]     = hum
        data_store["gas"]          = gas
        data_store["light_ao"]     = light_ao
        data_store["light_dark"]   = light_dark
        data_store["light_status"] = light_status
        data_store["last_updated"] = now
        for key, val in [("temp_history",  temp),
                         ("hum_history",   hum),
                         ("gas_history",   gas),
                         ("light_history", light_ao),
                         ("time_history",  now)]:
            data_store[key].append(val)
            if len(data_store[key]) > MAX:
                data_store[key].pop(0)

def read_arduino_sensors():
    import math, random
    now = datetime.now().strftime("%H:%M:%S")
    if not arduino or not arduino.is_open:
        t     = time.time()
        temp  = round(24.0 + 2.0 * math.sin(t / 30) + random.uniform(-0.2, 0.2), 1)
        hum   = round(45.0 + 5.0 * math.sin(t / 45) + random.uniform(-0.5, 0.5), 1)
        gas   = int(280  + 40  * math.sin(t / 60)  + random.uniform(-5, 5))
        light = int(200  + 150 * math.sin(t / 40)  + random.uniform(-10, 10))
        light = max(0, min(1023, light))
        update_store(temp, hum, gas, light, light > 600, now + " (demo)")
        return
    try:
        if arduino.in_waiting > 0:
            line = arduino.readline().decode('utf-8').strip()
            if "Temp:" in line and "Light:" in line:
                parts      = line.split("|")
                temp       = float(parts[0].replace("Temp:", "").replace("C", "").strip())
                hum        = float(parts[1].replace("Humidity:", "").replace("%", "").strip())
                gas        = int(parts[2].replace("Gas:", "").strip())
                light_ao   = int(parts[3].replace("Light:", "").strip())
                light_dark = int(parts[4].replace("Dark:", "").strip()) == 1
                update_store(temp, hum, gas, light_ao, light_dark, now)
    except:
        pass

# ============================================================
#  MEDIAPIPE + YOLO
# ============================================================

mp_pose       = mp.solutions.pose
mp_drawing    = mp.solutions.drawing_utils
pose_detector = mp_pose.Pose(min_detection_confidence=0.5,
                              min_tracking_confidence=0.5)

print("Loading YOLO model...")
yolo_model  = YOLO("yolov8n.pt")
PHONE_CLASS = 67
print("YOLO loaded!")

def check_posture(landmarks, fw, fh):
    nose = landmarks[mp_pose.PoseLandmark.NOSE]
    ls   = landmarks[mp_pose.PoseLandmark.LEFT_SHOULDER]
    rs   = landmarks[mp_pose.PoseLandmark.RIGHT_SHOULDER]
    if abs(ls.y * fh - rs.y * fh) > 30:
        return True, "Shoulder tilt"
    if abs(ls.x * fw - rs.x * fw) > 250:
        return True, "Too close to screen"
    avg_sh = (ls.y * fh + rs.y * fh) / 2
    if (avg_sh - nose.y * fh) < 80:
        return True, "Head drooping"
    return False, "Good"

def detect_phone(frame):
    for box in yolo_model(frame, verbose=False)[0].boxes:
        if int(box.cls[0]) == PHONE_CLASS and float(box.conf[0]) > 0.5:
            return True
    return False

# ============================================================
#  MAIN LOOP
# ============================================================

def main_loop():
    global latest_frame_bytes, webcam_active, detection_active

    cap          = None
    frame_count  = 0
    last_alert_t = 0
    phone_found  = False

    print("\n=== Smart Study Desk running! ===")
    print("Open browser → http://localhost:8080\n")

    while True:
        # ── Open / close webcam based on flag ──
        if webcam_active and (cap is None or not cap.isOpened()):
            cap = cv2.VideoCapture(0)
            if not cap.isOpened():
                print("Could not open webcam")
                webcam_active = False

        if not webcam_active:
            if cap and cap.isOpened():
                cap.release()
                cv2.destroyAllWindows()
                cap = None
                with frame_lock:
                    latest_frame_bytes = None
            read_arduino_sensors()   # keep charts alive even without webcam
            time.sleep(0.5)
            continue

        success, frame = cap.read()
        if not success:
            time.sleep(0.1)
            continue

        frame_count += 1
        h, w, _ = frame.shape

        read_arduino_sensors()

        posture_bad = False
        posture_msg = "Good"
        phone_found_now = False

        if detection_active:
            # Posture
            rgb          = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            pose_results = pose_detector.process(rgb)
            if pose_results.pose_landmarks:
                mp_drawing.draw_landmarks(frame, pose_results.pose_landmarks,
                                          mp_pose.POSE_CONNECTIONS)
                posture_bad, posture_msg = check_posture(
                    pose_results.pose_landmarks.landmark, w, h)

            # Phone every 10 frames
            if frame_count % 10 == 0:
                phone_found_now = detect_phone(frame)
                phone_found     = phone_found_now

        # Update store
        with data_lock:
            data_store["posture"]    = "Bad" if posture_bad else "Good"
            data_store["phone"]      = "Detected!" if phone_found else "Not Detected"
            data_store["webcam_on"]  = webcam_active
            data_store["detection_on"] = detection_active

            if posture_bad:
                data_store["alert"]       = f"Bad Posture: {posture_msg}"
                data_store["alert_type"]  = "posture"
                data_store["session_alerts"] = data_store["session_alerts"] + 1
                data_store["focus_score"] = max(0, data_store["focus_score"] - 1)
            elif phone_found:
                data_store["alert"]       = "Phone Detected — Stay Focused!"
                data_store["alert_type"]  = "phone"
                data_store["session_alerts"] = data_store["session_alerts"] + 1
                data_store["focus_score"] = max(0, data_store["focus_score"] - 2)
            else:
                data_store["alert"]       = "All Good — Keep Studying!"
                data_store["alert_type"]  = "good"
                data_store["focus_score"] = min(100, data_store["focus_score"] + 0.1)

        # Arduino alert
        now = time.time()
        if now - last_alert_t > 3:
            send_alert('P' if posture_bad else ('D' if phone_found else 'G'))
            last_alert_t = now

        # Status bar on frame
        col = (0, 200, 0) if not posture_bad and not phone_found else (0, 0, 255)
        cv2.rectangle(frame, (0, 0), (w, 36), (0, 0, 0), -1)
        cv2.putText(frame, data_store["alert"], (8, 24),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.65, col, 2)

        _, buf = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
        with frame_lock:
            latest_frame_bytes = buf.tobytes()

        cv2.imshow("Smart Study Desk", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    if cap:
        cap.release()
    cv2.destroyAllWindows()
    pose_detector.close()
    if arduino and arduino.is_open:
        send_alert('G')
        arduino.close()
    print("Stopped.")

# ============================================================
#  START
# ============================================================

if __name__ == "__main__":
    flask_thread = threading.Thread(target=run_flask, daemon=True)
    flask_thread.start()
    print("Flask server → http://localhost:8080")
    main_loop()