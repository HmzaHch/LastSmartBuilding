"""
LastSmartBuilding - ESP32-CAM Human Detection Server
Accepts RAW JPEG bytes from ESP32 (not multipart form)
"""

import os
import io
import base64
import uuid
import warnings
from datetime import datetime, timezone

import cv2
import numpy as np
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from gtts import gTTS
from ultralytics import YOLO

warnings.filterwarnings("ignore", category=RuntimeWarning)

# ═══════════════════════════════════════════════════════
# CONFIG
# ═══════════════════════════════════════════════════════
FIREBASE_KEY_FILE = "serviceAccountKey.json"
DATABASE_URL      = "https://smartbuilding-88e33-default-rtdb.firebaseio.com/"
RTDB_PATH         = "building/zone1/human_detection"
CONF_THRESHOLD    = 0.70
# ═══════════════════════════════════════════════════════

app = FastAPI(title="LastSmartBuilding")

# ── YOLOv8 ──
print("[INIT] Loading YOLOv8n...")
model = YOLO("yolov8n.pt")
print("[INIT] YOLO ready")

# ── Firebase RTDB ──
db_ref = None
firebase_ok = False

try:
    import firebase_admin
    from firebase_admin import credentials, db

    if os.path.exists(FIREBASE_KEY_FILE):
        if not firebase_admin._apps:
            cred = credentials.Certificate(FIREBASE_KEY_FILE)
            firebase_admin.initialize_app(cred, {'databaseURL': DATABASE_URL})
        db_ref = db.reference(RTDB_PATH)
        firebase_ok = True
        print(f"[INIT] Firebase RTDB OK: {RTDB_PATH}")
    else:
        print(f"[INIT] No Firebase key. Running WITHOUT cloud save.")
except Exception as e:
    print(f"[INIT] Firebase disabled: {e}")

def tts_to_b64(text: str, lang='en') -> str:
    try:
        mp3 = io.BytesIO()
        gTTS(text=text, lang=lang, slow=False).write_to_fp(mp3)
        mp3.seek(0)
        return base64.b64encode(mp3.read()).decode('utf-8')
    except Exception as e:
        print(f"[TTS] Error: {e}")
        return ""

# ═══════════════════════════════════════════════════════
# MAIN ENDPOINT — Accepts RAW JPEG bytes from ESP32
# ═══════════════════════════════════════════════════════
@app.post("/detect/human")
async def detect_human(request: Request):
    try:
        # Read raw JPEG bytes directly (no multipart parsing needed)
        data = await request.body()
        
        if len(data) < 100:
            print(f"[HTTP] Received only {len(data)} bytes — not a valid JPEG")
            return JSONResponse({
                "person": {"label": "error", "confidence": 0.0, "bbox": []},
                "audio_b64": "", "message": "Image too small", "objects": []
            })

        img = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
        if img is None:
            print("[HTTP] cv2.imdecode failed — invalid JPEG")
            return JSONResponse({
                "person": {"label": "error", "confidence": 0.0, "bbox": []},
                "audio_b64": "", "message": "Invalid JPEG", "objects": []
            })

        print(f"[HTTP] Received {len(data)} bytes, decoded {img.shape}")

        # Detect only class 0 = person
        results = model(img, classes=[0], verbose=False)

        best_conf = 0.0
        best_box = []
        objects = []

        for r in results:
            for box in r.boxes:
                conf = float(box.conf[0])
                label = r.names[int(box.cls[0])]
                xyxy = box.xyxy[0].tolist()
                objects.append({"label": label, "confidence": conf, "bbox": xyxy})
                if label == "person" and conf > best_conf:
                    best_conf = conf
                    best_box = xyxy

        is_human = best_conf >= CONF_THRESHOLD
        person = {
            "label": "person" if is_human else "none",
            "confidence": best_conf,
            "bbox": best_box
        }

        # ── FIREBASE RTDB ──
        human_answer = "yes" if is_human else "no"

        if firebase_ok and db_ref:
            try:
                db_ref.set({
                    "device": "esp32cam_01",
                    "zone": "zone1",
                    "human_detected": human_answer,
                    "confidence": round(best_conf, 4),
                    "timestamp_ms": int(datetime.now(timezone.utc).timestamp() * 1000),
                    "timestamp_iso": datetime.now(timezone.utc).isoformat(),
                    "status": "active" if is_human else "clear"
                })
                print(f"[FB] RTDB WRITTEN: {human_answer}")
            except Exception as e:
                print(f"[FB] RTDB write FAILED: {e}")
        else:
            print(f"[FB] SKIPPED (firebase_ok={firebase_ok})")

        # ── Response for ESP32 ──
        audio_b64 = ""
        message = "No human detected"

        if is_human:
            message = f"Human detected with {best_conf*100:.0f} percent confidence"
            audio_b64 = tts_to_b64(message)
            print(f"[DETECT] Human {best_conf:.2f} -> audio sent to ESP32")

        return JSONResponse({
            "person": person,
            "audio_b64": audio_b64,
            "objects": objects,
            "message": message,
            "detection_id": str(uuid.uuid4())[:8]
        })

    except Exception as e:
        print(f"[ERROR] {e}")
        import traceback
        traceback.print_exc()
        return JSONResponse({
            "person": {"label": "error", "confidence": 0.0, "bbox": []},
            "audio_b64": "", "objects": [],
            "message": f"Server error: {e}", "detection_id": ""
        })

@app.get("/health")
async def health():
    return {
        "status": "ok",
        "firebase": firebase_ok,
        "rtdb_path": RTDB_PATH,
        "threshold": CONF_THRESHOLD
    }

if __name__ == "__main__":
    print("\n" + "="*50)
    print("  LastSmartBuilding Server")
    print("="*50)
    print(f"Model:     yolov8n.pt")
    print(f"Threshold: {CONF_THRESHOLD}")
    print(f"Port:      8080")
    print(f"Firebase:  {'OK' if firebase_ok else 'OFFLINE'}")
    print(f"RTDB path: {RTDB_PATH}")
    print("="*50)
    import uvicorn
    uvicorn.run(app, host="192.168.100.37", port=8080)