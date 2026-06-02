/*
 * ESP32-CAM → LastSmartBuilding Server → Human Detection
 * ========================================================
 * 
 * BOARD: ESP32 Wrover Module
 * PARTITION: Huge APP (3MB No OTA / 1MB SPIFFS)
 * LIBRARY: ArduinoJson (v6.x) via Library Manager
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include <WebServer.h>

// ===================== EDIT THESE =====================
const char* WIFI_SSID     = "Hamza";
const char* WIFI_PASSWORD = "123456789";

// Your PC server IP (check with 'ipconfig' in PowerShell)
const char* SERVER_IP     = "192.168.100.37";   // ← UPDATE THIS
const int   SERVER_PORT   = 8080;
// =====================================================

String SERVER_URL;

// ── WebServer for mobile app ──
WebServer espServer(80);
volatile bool newAudioReady = false;
bool lastWasHuman = false;

// ── Stats ──
unsigned long framesSent  = 0;
unsigned long fpsCount    = 0;
unsigned long lastFpsTime = 0;
int errorCount = 0;
int totalHumans = 0;

// ── AI-Thinker Pin Map ──
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_LED_PIN      4

// ═══════════════════════════════════════════════════════
// WEB SERVER ROUTES (for mobile app)
// ═══════════════════════════════════════════════════════
void handleCheck() {
  String json = "{\"new_audio\":" + String(newAudioReady ? "true" : "false") + 
                ",\"human\":" + String(lastWasHuman ? "true" : "false") + "}";
  espServer.send(200, "application/json", json);
}

void handleAudio() {
  if (!SPIFFS.exists("/audio.mp3")) {
    espServer.send(404, "text/plain", "No audio");
    return;
  }
  File f = SPIFFS.open("/audio.mp3", FILE_READ);
  espServer.streamFile(f, "audio/mpeg");
  f.close();
  
  // CRITICAL: Reset flag ONLY after successful download
  newAudioReady = false;
  Serial.println("[APP] MP3 delivered to mobile");
}

void handleStatus() {
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"frames\":" + String(framesSent);
  json += ",\"humans\":" + String(totalHumans);
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"spiffs_kb\":" + String((SPIFFS.totalBytes()-SPIFFS.usedBytes())/1024,1);
  json += ",\"ready\":" + String(newAudioReady ? "true" : "false");
  json += "}";
  espServer.send(200, "application/json", json);
}

// ═══════════════════════════════════════════════════════
// BASE64 DECODER → SPIFFS
// ═══════════════════════════════════════════════════════
static const int8_t b64_table[256] = {
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
  52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
  -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
  15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
  -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
  41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

bool decodeBase64ToSPIFFS(const char* b64, size_t b64len, const char* path) {
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) {
    Serial.println("[B64] ERROR: Cannot open SPIFFS file");
    return false;
  }
  
  uint8_t buf[3];
  int bufLen = 0;
  size_t written = 0;
  
  for (size_t i = 0; i < b64len; i++) {
    char c = b64[i];
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int8_t val = b64_table[(uint8_t)c];
    if (val < 0) continue;
    
    switch (bufLen % 4) {
      case 0: buf[0] = val << 2; break;
      case 1: buf[0] |= val >> 4; buf[1] = (val & 0xF) << 4; break;
      case 2: buf[1] |= val >> 2; buf[2] = (val & 0x3) << 6; break;
      case 3: buf[2] |= val; f.write(buf, 3); written += 3; break;
    }
    bufLen++;
  }
  
  int rem = bufLen % 4;
  if (rem == 2) { f.write(buf, 1); written++; }
  if (rem == 3) { f.write(buf, 2); written += 2; }
  f.close();
  
  Serial.printf("[B64] Decoded %d bytes -> %s\n", written, path);
  return written > 0;
}

// ═══════════════════════════════════════════════════════
// CAMERA INIT (Optimized for Human/Face Detection)
// ═══════════════════════════════════════════════════════
void initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0 = Y2_GPIO_NUM;   cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM;   cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM;   cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM;   cfg.pin_d7 = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_SVGA;    // 800x600 — better for faces
  cfg.jpeg_quality = 10;                // Higher quality (lower number)
  cfg.fb_count     = 2;
  
  if (esp_camera_init(&cfg) != ESP_OK) {
    Serial.println("[CAM] FATAL: Camera init failed");
    ESP.restart();
  }
  
  // Sensor tuning for humans in varying light
  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 1);
  s->set_contrast(s, 1);
  s->set_saturation(s, 0);
  s->set_sharpness(s, 1);
  s->set_denoise(s, 1);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_gain_ctrl(s, 1);
  s->set_agc_gain(s, 0);
  s->set_gainceiling(s, (gainceiling_t)4);
  s->set_exposure_ctrl(s, 1);
  s->set_aec2(s, 1);
  s->set_ae_level(s, 0);
  s->set_bpc(s, 1);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  
  Serial.println("[CAM] SVGA ready for human detection");
}

// ═══════════════════════════════════════════════════════
// WIFI
// ═══════════════════════════════════════════════════════
void connectWifi() {
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  Serial.printf("[WiFi] Connecting to: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500); Serial.print("."); attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("[Server] Target: http://%s:%d/detect/human\n", SERVER_IP, SERVER_PORT);
  } else {
    Serial.println("[WiFi] FAILED — restarting in 3s...");
    delay(3000);
    ESP.restart();
  }
}

// ═══════════════════════════════════════════════════════
// SEND FRAME + PARSE HUMAN DETECTION
// ═══════════════════════════════════════════════════════
bool sendFrame(camera_fb_t* fb) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    return false;
  }
  
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(15000);
  
  Serial.printf("[HTTP] Uploading frame (%d bytes)...\n", fb->len);
  int code = http.POST(fb->buf, fb->len);
  
  if (code != 200) {
    Serial.printf("[HTTP] Error: %d\n", code);
    http.end();
    return false;
  }
  
  String body = http.getString();
  http.end();
  
  // Parse JSON — allocate based on response size
  size_t docSize = body.length() + 4096;
  DynamicJsonDocument doc(docSize);
  
  if (deserializeJson(doc, body)) {
    Serial.println("[JSON] Parse error");
    return false;
  }
  
  // ── HUMAN DETECTION PARSING ──
  // Server returns: {"person": {"label": "person", "confidence": 0.94, ...}}
  const char* label = doc["person"]["label"] | "none";
  float conf = doc["person"]["confidence"] | 0.0f;
  
  bool isHuman = (strcmp(label, "person") == 0) && (conf >= 0.70f);
  
  if (isHuman) {
    totalHumans++;
    lastWasHuman = true;
    Serial.printf("[HUMAN] Detected! %.0f%% (total: %d)\n", conf * 100, totalHumans);
    
    // Visual alert: flash LED twice
    for (int i = 0; i < 2; i++) {
      digitalWrite(FLASH_LED_PIN, HIGH); delay(80);
      digitalWrite(FLASH_LED_PIN, LOW);  delay(80);
    }
    
    // Decode audio if present
    const char* b64 = doc["audio_b64"] | "";
    size_t b64len = strlen(b64);
    
    if (b64len > 100) {
      Serial.printf("[AUDIO] Receiving %d chars base64\n", b64len);
      if (decodeBase64ToSPIFFS(b64, b64len, "/audio.mp3")) {
        newAudioReady = true;
        Serial.println("[AUDIO] Ready for mobile download");
      }
    }
  } else {
    lastWasHuman = false;
    Serial.printf("[DETECT] No human (found: %s @ %.0f%%)\n", label, conf * 100);
    
    // Clean stale audio so app doesn't get old alerts
    if (SPIFFS.exists("/audio.mp3")) {
      SPIFFS.remove("/audio.mp3");
      newAudioReady = false;
    }
  }
  
  // Print other detected objects for debug
  JsonArray arr = doc["objects"].as<JsonArray>();
  for (JsonObject o : arr) {
    Serial.printf("[OBJ] %s (%.0f%%)\n", (const char*)o["label"], (float)o["confidence"] * 100);
  }
  
  return true;
}

// ═══════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] FATAL: Mount failed");
    ESP.restart();
  }
  Serial.printf("[SPIFFS] Free: %.1f KB\n", (SPIFFS.totalBytes()-SPIFFS.usedBytes())/1024.0);
  
  // Clean old audio from previous boot
  if (SPIFFS.exists("/audio.mp3")) SPIFFS.remove("/audio.mp3");
  
  // Build server URL
  SERVER_URL = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT) + "/detect/human";
  
  initCamera();
  connectWifi();
  
  // Start WebServer for mobile app
  espServer.on("/check",     HTTP_GET, handleCheck);
  espServer.on("/audio.mp3", HTTP_GET, handleAudio);
  espServer.on("/status",    HTTP_GET, handleStatus);
  espServer.begin();
  
  lastFpsTime = millis();
  
  Serial.println("\n========================================");
  Serial.println("  HUMAN DETECTION SYSTEM READY");
  Serial.println("========================================");
  Serial.printf("Server: %s\n", SERVER_URL.c_str());
  Serial.printf("App check: http://%s/check\n", WiFi.localIP().toString().c_str());
  Serial.printf("App audio: http://%s/audio.mp3\n", WiFi.localIP().toString().c_str());
  Serial.println("========================================");
}

// ═══════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════
void loop() {
  // Always handle web client requests first (non-blocking)
  espServer.handleClient();
  
  // Capture frame
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Frame buffer error");
    delay(100);
    return;
  }
  
  // Send to server
  bool ok = sendFrame(fb);
  esp_camera_fb_return(fb);
  
  if (ok) {
    framesSent++; fpsCount++; errorCount = 0;
  } else {
    // Flush stale buffer on error
    camera_fb_t* flush = esp_camera_fb_get();
    if (flush) esp_camera_fb_return(flush);
    
    errorCount++;
    if (errorCount >= 5) {
      Serial.println("[ERR] Too many errors — reconnecting WiFi...");
      WiFi.disconnect(); delay(1000); connectWifi(); errorCount = 0;
    }
    delay(500);
  }
  
  // FPS reporting every 5 seconds
  unsigned long now = millis();
  if (now - lastFpsTime >= 5000) {
    float fps = fpsCount / ((now - lastFpsTime) / 1000.0f);
    Serial.printf("[STATS] Frames: %lu | FPS: %.1f | Humans: %d | RSSI: %d\n",
                  framesSent, fps, totalHumans, WiFi.RSSI());
    fpsCount = 0; lastFpsTime = now;
  }
  
  delay(200);
}