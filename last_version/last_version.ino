/* ============================================================
   Smart Building IoT - Zone 1, Node 01 (Main ESP32)
   + TWILIO SMS Alerts (Fixed with separate WiFiClientSecure)
   ============================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <BH1750.h>

/* ==================== CONFIGURATION ==================== */
const char *WIFI_SSID = "Hamza";
const char *WIFI_PASSWORD = "123456789";

const char *FIREBASE_HOST = "smartbuilding-88e33-default-rtdb.firebaseio.com";
const char *FIREBASE_DATABASE_SECRET = "ghmpNh23bQY9giBPbc5p3adfpIIBwHE3YOd1nYhG";

#define ZONE_ID "zone1"
#define DEVICE_ID "node_01"

/* --- TWILIO SMS Configuration --- */
#define SMS_ENABLED         true
#define SMS_PHONE_NUMBER    "+21623993737"
#define SMS_TWILIO_SID      "AC42b4d8e20eb0eca9cfc833383c10601e"
#define SMS_TWILIO_TOKEN    "e561d3e49a1b523ef035a00fde822a4c"
#define SMS_TWILIO_NUMBER   "+18382594027"

/* --- Pin Assignments --- */
#define PIN_DHT 4
#define PIN_MQ7_AO 34
#define PIN_MQ7_DO 35
#define PIN_BUZZER 25
#define PIN_RELAY 26

/* --- I2C Bus --- */
#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_FREQ 100000

/* --- I2C Addresses --- */
#define MPU6050_ADDR 0x68
#define GY30_ADDR 0x23

/* --- Intervals --- */
#define PUBLISH_INTERVAL_MS 5000
#define SECURITY_POLL_MS 3000
#define WIFI_TIMEOUT_MS 15000
#define SMS_COOLDOWN_MS 300000    // 5 minutes between SMS

/* --- Thresholds --- */
#define THRESH_TEMP_HIGH 50.0
#define THRESH_CO_RAW_HIGH 2000
#define THRESH_VIBRATION 2.5
#define THRESH_INCLINATION 15.0

/* --- Buzzer --- */
#define BUZZER_FREQ_HZ 2000
#define BUZZER_DURATION_MS 1000
#define BUZZER_REPEAT_MS 3000

/* ==================== GLOBALS ==================== */
DHT dht(PIN_DHT, DHT22);
BH1750 gy30;
HTTPClient http;
WiFiClientSecure client;
WiFiClientSecure smsClient;  // SEPARATE client for Twilio HTTPS

unsigned long lastPublish = 0;
unsigned long lastBuzzer = 0;
unsigned long lastSecurityPoll = 0;
unsigned long lastSMS = 0;
bool buzzerActive = false;
bool smsSent = false;

/* ==================== SETUP & LOOP ==================== */
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("[SYSTEM] Smart Building - Zone 1, Node 01");
  Serial.println("SMS Provider: Twilio");
  Serial.println("========================================");

  pinMode(PIN_MQ7_DO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_FREQ);
  initSensors();
  initBuzzer();
  connectWiFi();

  // Separate clients for Firebase and Twilio to avoid SSL conflicts
  client.setInsecure();
  smsClient.setInsecure();
  
  Serial.println("[SMS] Twilio configured");
  Serial.printf("[SMS] To: %s\n", SMS_PHONE_NUMBER);
  Serial.printf("[SMS] From: %s\n", SMS_TWILIO_NUMBER);
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Reconnecting...");
    connectWiFi();
  }

  handleBuzzer();

  if (now - lastSecurityPoll >= SECURITY_POLL_MS) {
    lastSecurityPoll = now;
    pollRelayOverride();
    pollHumanDetection();
  }

  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    publishAll();
  }
}

/* ==================== TWILIO SMS (FIXED) ==================== */
bool sendTwilioSMS(const char* messageBody) {
  if (!SMS_ENABLED) {
    Serial.println("[SMS] SMS disabled");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SMS] No WiFi");
    return false;
  }

  unsigned long now = millis();
  if (now - lastSMS < SMS_COOLDOWN_MS) {
    Serial.printf("[SMS] Cooldown: %lus remaining\n", 
                  (SMS_COOLDOWN_MS - (now - lastSMS)) / 1000);
    return false;
  }

  Serial.println("\n========== TWILIO SMS ==========");

  // Build URL
  String url = "https://api.twilio.com/2010-04-01/Accounts/";
  url += SMS_TWILIO_SID;
  url += "/Messages.json";
  
  Serial.printf("[SMS] URL: %s\n", url.c_str());

  // URL-encode message (simple version)
  String encodedMsg = String(messageBody);
  encodedMsg.replace(" ", "%20");
  encodedMsg.replace("!", "%21");
  encodedMsg.replace("#", "%23");
  encodedMsg.replace("$", "%24");
  encodedMsg.replace("&", "%26");
  encodedMsg.replace("'", "%27");
  encodedMsg.replace("(", "%28");
  encodedMsg.replace(")", "%29");
  encodedMsg.replace("*", "%2A");
  encodedMsg.replace("+", "%2B");
  encodedMsg.replace(",", "%2C");
  encodedMsg.replace("/", "%2F");
  encodedMsg.replace(":", "%3A");
  encodedMsg.replace(";", "%3B");
  encodedMsg.replace("=", "%3D");
  encodedMsg.replace("?", "%3F");
  encodedMsg.replace("@", "%40");
  encodedMsg.replace("[", "%5B");
  encodedMsg.replace("]", "%5D");

  // Build POST data
  String postData = "To=" + String(SMS_PHONE_NUMBER);
  postData += "&From=" + String(SMS_TWILIO_NUMBER);
  postData += "&Body=" + encodedMsg;

  Serial.printf("[SMS] To: %s\n", SMS_PHONE_NUMBER);
  Serial.printf("[SMS] From: %s\n", SMS_TWILIO_NUMBER);
  Serial.printf("[SMS] Body: %s\n", messageBody);

  // Use separate HTTP instance with separate client
  HTTPClient twilioHttp;
  twilioHttp.begin(smsClient, url);
  twilioHttp.setAuthorization(SMS_TWILIO_SID, SMS_TWILIO_TOKEN);
  twilioHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpCode = twilioHttp.POST(postData);
  String response = twilioHttp.getString();
  twilioHttp.end();

  Serial.printf("[SMS] HTTP Code: %d\n", httpCode);
  Serial.printf("[SMS] Response: %s\n", response.c_str());

  if (httpCode == 201) {
    Serial.println("[SMS] ✅ SUCCESS! SMS sent");
    lastSMS = now;
    return true;
  } else if (httpCode == 400) {
    Serial.println("[SMS] ❌ ERROR 400: Bad Request");
  } else if (httpCode == 401) {
    Serial.println("[SMS] ❌ ERROR 401: Unauthorized - check SID/Token");
  } else if (httpCode == 403) {
    Serial.println("[SMS] ❌ ERROR 403: Forbidden - trial account or unverified number");
    Serial.println("[SMS]    Go to https://www.twilio.com/console/phone-numbers/verified");
  } else if (httpCode == 404) {
    Serial.println("[SMS] ❌ ERROR 404: Not Found - check Account SID");
  } else if (httpCode == -1) {
    Serial.println("[SMS] ❌ ERROR -1: SSL/Connection failed");
  } else {
    Serial.printf("[SMS] ❌ ERROR %d\n", httpCode);
  }

  // Parse Twilio error JSON
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, response);
  if (!err) {
    if (doc["message"]) {
      Serial.printf("[SMS] Twilio message: %s\n", (const char*)doc["message"]);
    }
    if (doc["more_info"]) {
      Serial.printf("[SMS] More info: %s\n", (const char*)doc["more_info"]);
    }
    if (doc["code"]) {
      Serial.printf("[SMS] Error code: %d\n", (int)doc["code"]);
    }
  }

  Serial.println("================================\n");
  return false;
}

/* ==================== HUMAN DETECTION → RELAY ==================== */
void pollHumanDetection() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = "https://";
  url += FIREBASE_HOST;
  url += "/building/zone1/human_detection.json";
  if (strlen(FIREBASE_DATABASE_SECRET) > 0) {
    url += "?auth=";
    url += FIREBASE_DATABASE_SECRET;
  }

  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    payload.trim();

    if (payload == "null") {
      http.end();
      return;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      http.end();
      return;
    }

    const char* humanDetected = doc["human_detected"] | "no";
    float confidence = doc["confidence"] | 0.0;
    
    Serial.printf("[HUMAN] '%s' (%.0f%%)\n", humanDetected, confidence * 100);

    if (strcmp(humanDetected, "yes") == 0) {
      digitalWrite(PIN_RELAY, HIGH);
      Serial.println("[RELAY] ON — human detected");
    } else {
      digitalWrite(PIN_RELAY, LOW);
      Serial.println("[RELAY] OFF — clear");
    }
  }
  http.end();
}

void pollRelayOverride() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = "https://";
  url += FIREBASE_HOST;
  url += "/building/zone1/relay_override/state.json";
  if (strlen(FIREBASE_DATABASE_SECRET) > 0) {
    url += "?auth=";
    url += FIREBASE_DATABASE_SECRET;
  }

  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    payload.trim();
    if (payload.startsWith("\"")) payload = payload.substring(1);
    if (payload.endsWith("\"")) payload = payload.substring(0, payload.length() - 1);

    if (payload == "on") {
      digitalWrite(PIN_RELAY, HIGH);
    } else if (payload == "off") {
      digitalWrite(PIN_RELAY, LOW);
    }
  }
  http.end();
}

/* ==================== BUZZER ==================== */
void initBuzzer() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

void triggerBuzzer() {
  buzzerActive = true;
  lastBuzzer = millis();
  tone(PIN_BUZZER, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
  Serial.println("[ALERT] Buzzer!");
}

void stopBuzzer() {
  buzzerActive = false;
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

void handleBuzzer() {
  if (!buzzerActive) return;
  if (millis() - lastBuzzer >= BUZZER_REPEAT_MS) {
    lastBuzzer = millis();
    tone(PIN_BUZZER, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
  }
}

/* ==================== SENSORS ==================== */
void initSensors() {
  dht.begin();
  if (gy30.begin(BH1750::CONTINUOUS_HIGH_RES_MODE_2, GY30_ADDR, &Wire)) {
    Serial.println("[SENSORS] BH1750 OK");
  } else {
    Serial.println("[ERROR] BH1750 not found");
  }

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  if (Wire.endTransmission(true) == 0) {
    Serial.println("[SENSORS] MPU-6050 OK");
  } else {
    Serial.println("[ERROR] MPU-6050 not found");
  }
}

int readMQ7() {
  return analogRead(PIN_MQ7_AO);
}

float readLightGY30() {
  float lux = gy30.readLightLevel();
  if (lux < 0 || lux > 65535.0) return -1.0;
  return lux;
}

bool readMPU6050(float &vibration, float &inclination, float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU6050_ADDR, (uint8_t)14, (bool)true) < 14) return false;

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();
  for (int i = 0; i < 8; i++) Wire.read();

  const float ACCEL_SCALE = 16384.0;
  ax = rawX / ACCEL_SCALE;
  ay = rawY / ACCEL_SCALE;
  az = rawZ / ACCEL_SCALE;

  float mag = sqrt(ax * ax + ay * ay + az * az);
  vibration = abs(mag - 1.0);
  inclination = acos(constrain(az / mag, -1.0, 1.0)) * 180.0 / PI;
  return true;
}

/* ==================== NETWORK ==================== */
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WIFI] Connecting");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WIFI] Timeout!");
  }
}

/* ==================== FIREBASE ==================== */
bool sendToFirebase(const char *path, JsonDocument &doc) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String payload;
  serializeJson(doc, payload);

  String url = "https://";
  url += FIREBASE_HOST;
  url += path;
  url += ".json";
  if (strlen(FIREBASE_DATABASE_SECRET) > 0) {
    url += "?auth=";
    url += FIREBASE_DATABASE_SECRET;
  }

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PUT(payload);

  bool ok = (httpCode == 200);
  http.end();
  return ok;
}

/* ==================== PUBLISH + TWILIO SMS ==================== */
void publishAll() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int coRaw = readMQ7();
  bool coDigital = digitalRead(PIN_MQ7_DO);
  float lightLux = readLightGY30();

  float vib, inc, ax, ay, az;
  bool mpuOK = readMPU6050(vib, inc, ax, ay, az);

  if (!isnan(temperature) && !isnan(humidity)) {
    JsonDocument doc;
    doc["device"] = DEVICE_ID;
    doc["zone"] = ZONE_ID;
    doc["temp_c"] = temperature;
    doc["hum_pct"] = humidity;
    doc["light_lux"] = lightLux;
    doc["co_raw"] = coRaw;
    doc["co_digital"] = coDigital;
    doc["timestamp_ms"] = millis();
    sendToFirebase("/building/zone1/environmental", doc);
  }

  if (mpuOK) {
    JsonDocument doc;
    doc["device"] = DEVICE_ID;
    doc["zone"] = ZONE_ID;
    doc["vibration_g"] = vib;
    doc["inclination_deg"] = inc;
    doc["accel_x"] = ax;
    doc["accel_y"] = ay;
    doc["accel_z"] = az;
    doc["timestamp_ms"] = millis();
    sendToFirebase("/building/zone1/structural", doc);
  }

  if (!isnan(temperature)) checkThresholds(temperature, coRaw, vib, inc);

  Serial.printf("[DATA] T:%.1fC H:%.1f%% CO:%d L:%.1f Vib:%.3f Inc:%.1f\n",
                temperature, humidity, coRaw, lightLux, vib, inc);
}

void checkThresholds(float temp, int coRaw, float vib, float inc) {
  bool alert = false;
  String alertMsg = "ALERT ";

  if (temp > THRESH_TEMP_HIGH) {
    publishAlert("CRITICAL", "High Temperature", temp);
    alertMsg += "TEMP:" + String(temp, 1) + "C ";
    alert = true;
  }
  if (coRaw > THRESH_CO_RAW_HIGH) {
    publishAlert("CRITICAL", "High CO Level", coRaw);
    alertMsg += "CO:" + String(coRaw) + " ";
    alert = true;
  }
  if (vib > THRESH_VIBRATION) {
    publishAlert("CRITICAL", "Structural Vibration", vib);
    alertMsg += "VIB:" + String(vib, 2) + "g ";
    alert = true;
  }
  if (inc > THRESH_INCLINATION) {
    publishAlert("CRITICAL", "Structural Inclination", inc);
    alertMsg += "INC:" + String(inc, 1) + "° ";
    alert = true;
  }

  if (alert) {
    triggerBuzzer();
    if (!smsSent) {
      alertMsg += "| " + String(ZONE_ID) + "/" + String(DEVICE_ID);
      sendTwilioSMS(alertMsg.c_str());
      smsSent = true;
    }
  } else {
    if (buzzerActive) stopBuzzer();
    smsSent = false;
  }
}

void publishAlert(const char *level, const char *msg, float value) {
  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["zone"] = ZONE_ID;
  doc["level"] = level;
  doc["message"] = msg;
  doc["value"] = value;
  doc["timestamp_ms"] = millis();
  sendToFirebase("/building/zone1/alerts", doc);
}