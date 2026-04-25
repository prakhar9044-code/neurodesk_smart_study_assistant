# ============================================================
#  AI-Enabled Smart Study Desk - Python Script
#  Features: Flask dashboard + webcam/detection control via web
#
#  Install: pip install opencv-python mediapipe pyserial flask ultralytics
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
    "gas"          : 0,
    "light_ao"     : 0,
    "light_dark"   : False,
    "light_status" : "Good",
    "posture"      : "Good",
    "phone"        : "Not Detected",
    "alert"        : "System Ready",
    "alert_type"   : "good",
    "last_updated" : "",
    "temp_history" : [],
    "hum_history"  : [],
    "gas_history"  : [],
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


@app.route("/chat", methods=["POST"])
def chat():
    msg  = request.get_json().get("message","").lower().strip()
    with data_lock:
        snap = dict(data_store)
    r = neurodesk_chat(msg, snap)
    return jsonify({"response": r, "timestamp": datetime.now().strftime("%H:%M:%S")})


def neurodesk_chat(msg, d):
    t = d.get("temperature", 0)
    h = d.get("humidity", 0)
    g = d.get("gas", 0)
    l = d.get("light_ao", 0)
    fs = d.get("focused_seconds", 0)
    ps = d.get("phone_seconds", 0)
    score = d.get("focus_score", 100)
    focused_min = fs // 60
    phone_min   = ps // 60

    greet = ["hi","hello","hey","namaste","good morning","good evening","what's up","sup"]
    if any(w in msg for w in greet):
        return f"Hello! I am NEURODESK AI 🧠 — your intelligent study companion. You have been focused for {focused_min} minutes this session. How can I help you?"

    if any(w in msg for w in ["temp","hot","cold","heat","warm"]):
        s = "comfortable ✓" if 18<=t<=28 else ("too hot ⚠️ — consider opening a window" if t>28 else "quite cold ❄️ — may affect concentration")
        return f"Current temperature is {t:.1f}°C — {s}. Optimal study temperature is 20–25°C for best cognitive performance."

    if any(w in msg for w in ["humid","moisture","dry"]):
        s = "Good ✓" if 30<=h<=60 else ("Too humid ⚠️" if h>60 else "Too dry ⚠️")
        return f"Humidity is {h:.1f}% — {s}. Ideal range is 30–60%. High humidity causes drowsiness; low humidity causes eye strain."

    if any(w in msg for w in ["air","gas","pollution","co2","co","quality","breathe","oxygen"]):
        s = "Excellent 🌿" if g<200 else ("Good ✓" if g<350 else ("Moderate ⚠️" if g<500 else "Poor 🚨 — ventilate immediately!"))
        return f"Air quality index: {g} — {s}. CO₂ estimated ~{400+int(g*0.5)} ppm. {'Open a window!' if g>400 else 'Air is clean.'}"

    if any(w in msg for w in ["light","dark","bright","lux","lamp"]):
        s = "Good lighting ✓" if l<400 else "Too dark ⚠️ — turn on more lights to reduce eye strain"
        return f"Light level: {l} lux — {s}. Ideal desk lighting is 300–500 lux for study."

    if any(w in msg for w in ["focus","focused","study time","how long","duration","session"]):
        return f"This session: {focused_min} min focused 🎯, {phone_min} min distracted 📱. Focus score: {int(score)}/100. {'Great work! Keep it up 🔥' if score>70 else 'Try to reduce distractions for a better score.'}"

    if any(w in msg for w in ["phone","distract","mobile","social media"]):
        return f"Phone usage detected {phone_min} minutes this session. 📱 Each phone check breaks your concentration for ~23 minutes. Try putting your phone in another room!"

    if any(w in msg for w in ["posture","back","neck","spine","slouch","sit","screen","distance"]):
        st = d.get("posture","Good")
        return f"Current posture status: {st}. {'⚠️ Please sit up straight! ' if st!='Good' else '✓ Good posture detected. '}Tips: Keep your back straight, feet flat on floor, screen at eye level, and take a break every 30 minutes."

    if any(w in msg for w in ["tip","advice","help","improve","productivity","study"]):
        tips = [
            "Use the Pomodoro technique: 25 min focused work + 5 min break 🍅",
            "Keep your study room temperature between 20–25°C for optimal brain function 🧠",
            "Drink water every 30 minutes — dehydration reduces focus by up to 20% 💧",
            "Natural light is best for studying. Face a window if possible ☀️",
            "Take a 5-minute walk break every hour to boost creativity 🚶",
            "Keep your phone in another room — even its presence reduces IQ by 10 points 📵",
            "Follow the 20-20-20 rule to protect your eyes: Every 20 minutes, look at something 20 feet away for 20 seconds 👀",
        ]
        import random
        return random.choice(tips)

    if any(w in msg for w in ["score","rating","performance","how am i","doing"]):
        grade = "A+ 🏆" if score>=90 else ("A 🌟" if score>=80 else ("B 👍" if score>=70 else ("C ⚠️" if score>=50 else "D 🚨")))
        return f"Your current focus score is {int(score)}/100 — Grade: {grade}. Focused time: {focused_min} min, Distracted: {phone_min} min."

    if any(w in msg for w in ["sensor","arduino","hardware","component"]):
        return "NEURODESK uses: DHT22 (temp/humidity), MQ135 (air quality), LDR module (light level), RGB LED (status indicator), Active buzzer (alerts), 16×2 I2C LCD, and a webcam for AI posture + phone detection 🔬"

    if any(w in msg for w in ["what are you","who are you","neurodesk","yourself","about"]):
        return "I am NEURODESK AI 🧠 — an intelligent study environment monitor. I track posture, phone usage, air quality, temperature, humidity, and light to keep your study sessions optimized. Ask me anything!"

    if any(w in msg for w in ["bye","goodbye","thanks","thank you","cya"]):
        return f"Great session today! {focused_min} minutes of focused work. Keep up the great work! 🚀 NEURODESK is always here for you."

    if any(w in msg for w in ["break","rest","tired","sleepy","exhausted"]):
        return f"You have been studying for {focused_min} minutes. {'A short break is well deserved! 5 min rest → better retention. 🌿' if focused_min > 25 else 'Push through for a bit longer before your break! 💪'}"

    return f"I am NEURODESK AI 🧠. Current status — Temp: {t:.1f}°C | Humidity: {h:.1f}% | Air: {'Good' if g<400 else 'Poor'} | Focus: {int(score)}/100. Ask me about your sensors, study tips, posture, or performance!"


@app.route("/control", methods=["POST"])
def control():
    global webcam_active, detection_active
    body   = request.get_json()
    action = body.get("action", "")

    if action == "start_webcam":
        webcam_active = True
    elif action == "stop_webcam":
        webcam_active    = False
        detection_active = False
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

try:
    yolo_model = YOLO('yolov8n.pt') 
    print("YOLOv8 Nano loaded for phone detection")
except Exception as e:
    print(f"Failed to load YOLO: {e}")
    yolo_model = None

def check_posture(landmarks, fw, fh):
    ls = landmarks[mp_pose.PoseLandmark.LEFT_SHOULDER]
    rs = landmarks[mp_pose.PoseLandmark.RIGHT_SHOULDER]
    le = landmarks[mp_pose.PoseLandmark.LEFT_EAR]
    re = landmarks[mp_pose.PoseLandmark.RIGHT_EAR]
    
    l_eye = landmarks[mp_pose.PoseLandmark.LEFT_EYE]
    r_eye = landmarks[mp_pose.PoseLandmark.RIGHT_EYE]

    if ls.visibility < 0.3 or rs.visibility < 0.3 or le.visibility < 0.3 or re.visibility < 0.3:
        return True, "Adjust Camera: Shoulders/Ears not visible"

    avg_ear_y = (le.y + re.y) / 2.0
    avg_sh_y = (ls.y + rs.y) / 2.0
    
    neck_extension = avg_sh_y - avg_ear_y 
    tilt = abs(ls.y - rs.y)
    
    eye_distance = ((l_eye.x - r_eye.x)**2 + (l_eye.y - r_eye.y)**2)**0.5

    print(f"[POSTURE DATA] Neck Ext: {neck_extension:.3f} | Tilt: {tilt:.3f} | Eye Dist: {eye_distance:.3f}")

    if eye_distance > 0.12:
        return True, "Too close to screen!"

    if tilt > 0.08:
        return True, "Shoulder tilt"

    if neck_extension < 0.12:
        return True, "Slouching / Neck hunch"

    return False, "Good"

def detect_phone(frame):
    if yolo_model is None:
        return False
        
    try:
        results = yolo_model(frame, verbose=False)
        
        for result in results:
            for box in result.boxes:
                class_id = int(box.cls[0])
                confidence = float(box.conf[0])
                
                # Class 67 is 'cell phone'
                if class_id == 67:
                    print(f"[PHONE SEEN] Confidence: {confidence:.2f}")
                    
                    # Lowered threshold to 0.25
                    if confidence > 0.25:
                        return True
                    
        return False
        
    except Exception as e:
        print(f"[PHONE ERROR] {e}")
        return False


def main_loop():
    global latest_frame_bytes, webcam_active, detection_active

    cap          = None
    frame_count  = 0
    last_alert_t = 0
    
    last_phone_time = 0 

    print("\n=== Smart Study Desk running! ===")
    print("Open browser → http://localhost:8080\n")

    while True:
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
            read_arduino_sensors()
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
        
        if detection_active:
            # Posture & Screen Distance
            rgb          = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            pose_results = pose_detector.process(rgb)
            if pose_results.pose_landmarks:
                mp_drawing.draw_landmarks(frame, pose_results.pose_landmarks,
                                          mp_pose.POSE_CONNECTIONS)
                posture_bad, posture_msg = check_posture(
                    pose_results.pose_landmarks.landmark, w, h)

            # Phone detection - changed to check every 3 frames for better tracking!
            if frame_count % 3 == 0:
                if detect_phone(frame):
                    last_phone_time = time.time()

        # INCREASED LOGIC: Memory cooldown is now 6 seconds instead of 3.
        # This keeps the alert active even if YOLO loses sight of the stationary phone briefly.
        phone_found = (time.time() - last_phone_time) < 6.0

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