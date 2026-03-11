/*
 * ESP32 Soil Sensor 7-in-1
 * RS485 Raw Modbus + LCD 20x4 I2C + LED + WiFi + WebSocket
 *
 * Libraries:
 *   - LiquidCrystal_I2C  (Frank de Brabander)
 *   - WebSockets         (Markus Sattler)
 *   - ArduinoJson
 */

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ─── RS485 ────────────────────────────────────────────────
#define RXD2  16
#define TXD2  17
#define DE    18
#define RE    19

// ─── LED ──────────────────────────────────────────────────
#define LED_RED    13
#define LED_YELLOW 12
#define LED_GREEN  14

// ─── WiFi ─────────────────────────────────────────────────
const char* WIFI_SSID     = "Wifi_Office 5GHz";
const char* WIFI_PASSWORD = "5tgb@1234567890";

// ─── Modbus request frame ─────────────────────────────────
byte request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};

// ─── LCD + WebSocket ──────────────────────────────────────
LiquidCrystal_I2C  lcd(0x27, 20, 4);
WebSocketsServer   webSocket(81);

// ─── Soil data ────────────────────────────────────────────
struct SoilData {
  float moisture;
  float temperature;
  float ec;
  float ph;
  float nitrogen;
  float phosphorus;
  float potassium;
};

SoilData soil = {0};
bool     dataReady = false;

// ─── Timing (non-blocking) ────────────────────────────────
unsigned long lastRequest  = 0;
unsigned long lastBroadcast = 0;
unsigned long lastLcdSwitch = 0;
unsigned long lastReconnect = 0;

const unsigned long REQUEST_INTERVAL  = 3000;   // อ่าน sensor ทุก 3s
const unsigned long BROADCAST_INTERVAL = 2000;  // broadcast WS ทุก 2s
const unsigned long LCD_SWITCH_INTERVAL = 4000; // สลับหน้า LCD ทุก 4s

int lcdPage = 0;

// ─── RS485: send request ─────────────────────────────────
void sendRequest() {
  digitalWrite(DE, HIGH);
  digitalWrite(RE, HIGH);
  delay(10);
  Serial2.write(request, sizeof(request));
  Serial2.flush();
  digitalWrite(DE, LOW);
  digitalWrite(RE, LOW);
}

// ─── Parse raw response bytes ────────────────────────────
SoilData parseResponse(byte* data) {
  SoilData s;
  s.moisture    = ((data[3]  << 8) | data[4])  / 10.0;
  s.temperature = ((data[5]  << 8) | data[6])  / 10.0;
  s.ec          = ((data[7]  << 8) | data[8])  / 1.0;
  s.ph          = ((data[9]  << 8) | data[10]) / 10.0;
  s.nitrogen    = ((data[11] << 8) | data[12]) / 1.0;
  s.phosphorus  = ((data[13] << 8) | data[14]) / 1.0;
  s.potassium   = ((data[15] << 8) | data[16]) / 1.0;
  return s;
}

// ─── LCD pages ────────────────────────────────────────────
void displayPage1(SoilData s) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.printf("Moist : %.1f %%",    s.moisture);
  lcd.setCursor(0, 1); lcd.printf("Temp  : %.1f C",     s.temperature);
  lcd.setCursor(0, 2); lcd.printf("EC    : %.0f us/cm", s.ec);
  lcd.setCursor(0, 3); lcd.printf("pH    : %.1f",       s.ph);
}

void displayPage2(SoilData s) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("-- NPK (mg/kg) --");
  lcd.setCursor(0, 1); lcd.printf("N : %.0f", s.nitrogen);
  lcd.setCursor(0, 2); lcd.printf("P : %.0f", s.phosphorus);
  lcd.setCursor(0, 3); lcd.printf("K : %.0f", s.potassium);
}

void displayPage3() {
  // Page 3: IP address (แสดงหลัง page 2)
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("-- WiFi Info --");
  lcd.setCursor(0, 1); lcd.print("IP:");
  lcd.setCursor(0, 2); lcd.print(WiFi.localIP().toString());
  lcd.setCursor(0, 3); lcd.print("WS Port: 81");
}

// ─── LED by moisture ─────────────────────────────────────
void updateLED(float moisture) {
  digitalWrite(LED_RED,    LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN,  LOW);
  if      (moisture < 30.0)  digitalWrite(LED_RED,    HIGH);
  else if (moisture <= 80.0) digitalWrite(LED_YELLOW, HIGH);
  else                       digitalWrite(LED_GREEN,  HIGH);
}

// ─── WebSocket: broadcast JSON ───────────────────────────
void broadcastJSON() {
  if (!dataReady) return;

  StaticJsonDocument<256> doc;
  doc["moisture"]    = round(soil.moisture    * 10) / 10.0;
  doc["temperature"] = round(soil.temperature * 10) / 10.0;
  doc["ec"]          = (int)soil.ec;
  doc["ph"]          = round(soil.ph          * 10) / 10.0;
  doc["nitrogen"]    = (int)soil.nitrogen;
  doc["phosphorus"]  = (int)soil.phosphorus;
  doc["potassium"]   = (int)soil.potassium;

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

// ─── WebSocket event ─────────────────────────────────────
void onWSEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[WS] Client #%u connected\n", num);
    broadcastJSON(); // ส่งข้อมูลล่าสุดทันทีที่ client เชื่อมต่อ
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[WS] Client #%u disconnected\n", num);
  }
}

// ─── WiFi connect ────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting WiFi");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Connecting WiFi...");
  lcd.setCursor(0, 1); lcd.print(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK — IP: %s\n", WiFi.localIP().toString().c_str());
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print("IP:");
    lcd.setCursor(0, 2); lcd.print(WiFi.localIP().toString());
    lcd.setCursor(0, 3); lcd.print("WS Port: 81");
    delay(3000);
  } else {
    Serial.println("\nWiFi FAILED — offline mode");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi FAILED");
    lcd.setCursor(0, 1); lcd.print("Offline mode");
    delay(2000);
  }
}

// ─── Setup ───────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // LED
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  digitalWrite(LED_RED, HIGH); // RED on during boot

  // LCD
  Wire.begin();
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 1); lcd.print("  Soil Sensor 7in1  ");
  lcd.setCursor(0, 2); lcd.print("   Initializing...  ");
  delay(2000);

  // RS485
  Serial2.begin(4800, SERIAL_8N1, RXD2, TXD2);
  pinMode(DE, OUTPUT);
  pinMode(RE, OUTPUT);
  digitalWrite(DE, LOW);
  digitalWrite(RE, LOW);

  // WiFi + WebSocket
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    webSocket.begin();
    webSocket.onEvent(onWSEvent);
    Serial.println("WebSocket server started on port 81");
  }

  digitalWrite(LED_RED, LOW);
}

// ─── Loop ────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // WebSocket
  if (WiFi.status() == WL_CONNECTED) {
    webSocket.loop();
  }

  // WiFi auto-reconnect
  if (WiFi.status() != WL_CONNECTED && now - lastReconnect > 10000) {
    lastReconnect = now;
    Serial.println("WiFi lost — reconnecting...");
    WiFi.reconnect();
  }

  // อ่าน sensor
  if (now - lastRequest >= REQUEST_INTERVAL) {
    lastRequest = now;
    sendRequest();
    delay(200); // รอ response

    if (Serial2.available() >= 19) {
      byte buf[19];
      Serial2.readBytes(buf, 19);
      soil = parseResponse(buf);
      dataReady = true;

      // Serial Monitor
      Serial.println("========== Soil Data ==========");
      Serial.printf("Moisture    : %.1f %%\n",    soil.moisture);
      Serial.printf("Temperature : %.1f C\n",     soil.temperature);
      Serial.printf("EC          : %.0f us/cm\n", soil.ec);
      Serial.printf("pH          : %.1f\n",       soil.ph);
      Serial.printf("Nitrogen    : %.0f mg/kg\n", soil.nitrogen);
      Serial.printf("Phosphorus  : %.0f mg/kg\n", soil.phosphorus);
      Serial.printf("Potassium   : %.0f mg/kg\n", soil.potassium);
      Serial.println("================================");

      updateLED(soil.moisture);
    }
  }

  // Broadcast WebSocket ทุก 2s
  if (dataReady && now - lastBroadcast >= BROADCAST_INTERVAL) {
    lastBroadcast = now;
    if (WiFi.status() == WL_CONNECTED) {
      broadcastJSON();
    }
  }

  // สลับหน้า LCD ทุก 4s (3 หน้า: sensor1, sensor2, wifi)
  if (now - lastLcdSwitch >= LCD_SWITCH_INTERVAL) {
    lastLcdSwitch = now;
    if (dataReady) {
      switch (lcdPage) {
        case 0: displayPage1(soil); break;
        case 1: displayPage2(soil); break;
        case 2: displayPage3();     break;
      }
      lcdPage = (lcdPage + 1) % 3;
    }
  }
}
