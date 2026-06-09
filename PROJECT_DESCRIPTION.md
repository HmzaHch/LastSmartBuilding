# LastSmartBuilding Project Description

## Overview
LastSmartBuilding is an ESP32-CAM human detection system for a smart building environment.
The ESP32-CAM sends camera frames to a Python server for YOLOv8 inference. When a person is detected, the server can return audio alerts and optionally update Firebase Realtime Database.

## What this project does
- Captures images from an ESP32-CAM camera module
- Sends JPEG frames to a local Python server via HTTP
- Detects people using YOLOv8 on the server
- Sends detection results and optional audio back to the ESP32-CAM
- Stores detection state in Firebase RTDB when configured
- Provides a dashboard for monitoring zone status

## Main components

### 1. ESP32-CAM firmware (`cam01/cam01.ino`)
- Captures and sends JPEG frames to the server
- Hosts a simple web API for mobile app status and audio retrieval
- Uses onboard flash LED for human detection alert
- Stores audio files in SPIFFS

### 2. Python server (`server.py`)
- Receives raw JPEG bytes at `/detect/human`
- Runs YOLOv8n inference on each frame
- Returns JSON with detection metadata, bounding box, and optional `audio_b64`
- Exposes `/health` for service status
- Writes detection state to Firebase RTDB when `serviceAccountKey.json` is present

### 3. Dashboard (`index.html`)
- Visualizes building zone status
- Displays detection state and sensor data cards
- Designed for Zone 1 monitoring

### 4. Firebase Realtime Database
- Stores detection status at `building/zone1/human_detection`
- Requires valid Firebase service credentials

### 5. YOLOv8 model (`yolov8n.pt`)
- Lightweight person detector
- Configured to detect only class `person`

## How it works

### ESP32-CAM flow
1. Mount SPIFFS and initialize the camera
2. Connect to Wi-Fi
3. Build the detection URL using `SERVER_IP` and `SERVER_PORT`
4. Continuously capture frames
5. Send each frame as raw `image/jpeg` to `/detect/human`
6. Receive JSON response
7. If a person is detected:
   - flash the LED
   - save `audio_b64` to `/audio.mp3`
   - mark the audio as ready for the mobile app
8. Serve `/check`, `/audio.mp3`, and `/status` endpoints locally

### Server flow
1. Load the model from `yolov8n.pt`
2. Optionally initialize Firebase using `serviceAccountKey.json`
3. Accept raw JPEG uploads at `/detect/human`
4. Decode JPEG into an OpenCV image
5. Run YOLO inference for class `person`
6. Build the response JSON with:
   - `person` result
   - `objects` array
   - `audio_b64` when a human is detected
   - `message`
   - short `detection_id`
7. Write detection status to Firebase RTDB when enabled

## Data flow schematic

```text
[ESP32-CAM Camera] -- JPEG frame --> [ESP32-CAM Firmware]
                                    |
                                    v
                           [Wi-Fi Router / Network]
                                    |
                                    v
                 [Python Server (`server.py`) - /detect/human]
                                    |
                                    v
                  [YOLOv8 Detection + optional TTS audio]
                                    |
                                    v
                   [JSON response + audio_b64 to ESP32-CAM]
                                    |
                                    v
                         [ESP32-CAM local app / mobile client]

Optional:
[Python Server] --> [Firebase RTDB] --> [Cloud detection state]

Optional dashboard access:
[Browser / Dashboard] --> [Firebase RTDB or server endpoints]
```

![Data Flow Diagram](DATA_FLOW_DIAGRAM.svg)

## Hardware overview

### ESP32-CAM connections
- `GPIO 0`  -> XCLK
- `GPIO 26` -> SIOD
- `GPIO 27` -> SIOC
- `GPIO 35` -> Y9
- `GPIO 34` -> Y8
- `GPIO 39` -> Y7
- `GPIO 36` -> Y6
- `GPIO 21` -> Y5
- `GPIO 19` -> Y4
- `GPIO 18` -> Y3
- `GPIO 5`  -> Y2
- `GPIO 25` -> VSYNC
- `GPIO 23` -> HREF
- `GPIO 22` -> PCLK
- `GPIO 32` -> PWDN
- `GPIO 4`  -> Flash LED
- `GPIO -1` -> Reset not used

### Physical system
- ESP32-CAM receives power from a 5V supply
- Camera module attaches to the ESP32-CAM camera pins
- Flash LED connects to `GPIO 4`
- ESP32-CAM connects to the local Wi-Fi network
- Python server runs on a PC on the same LAN
- Firebase RTDB is optional cloud storage for detection results

## Key files
- `server.py` - main detection backend
- `index.html` - dashboard interface
- `requirements.txt` - Python dependencies
- `cam01/cam01.ino` - ESP32-CAM firmware
- `yolov8n.pt` - detection model
- `serviceAccountKey.json` - Firebase credentials (optional)
- `PROJECT_DESCRIPTION.md` - project documentation
- `DATA_FLOW_DIAGRAM.svg` - visual data flow schematic

## Dependencies

Python packages:
- fastapi
- uvicorn[standard]
- python-multipart
- opencv-python
- numpy
- ultralytics
- gtts
- firebase-admin
- Pillow

ESP32 libraries:
- `esp_camera`
- `WiFi`
- `HTTPClient`
- `SPIFFS`
- `ArduinoJson`
- `WebServer`

## Setup steps
1. Update Wi-Fi credentials in `cam01/cam01.ino`
2. Set `SERVER_IP` to the PC running `server.py`
3. Install Python dependencies from `requirements.txt`
4. Run `python server.py`
5. Upload the firmware to the ESP32-CAM
6. Access the dashboard or mobile client for status and audio

## Expected behavior
- ESP32-CAM uploads frames to the server continuously
- The server detects humans and sends back detection results
- Detected human events may produce audio alerts
- Firebase RTDB stores current detection state when enabled
- Dashboard shows the zone status clearly
