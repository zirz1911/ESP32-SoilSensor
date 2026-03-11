/*
 * ESP32 Soil Sensor 7-in-1 with WiFi + WebSocket
 * RS485 Modbus RTU + LCD 20x4 + LED Indicators
 *
 * Libraries required:
 *   - ModbusMaster       (Doc Walker)
 *   - LiquidCrystal_I2C  (Frank de Brabander)
 *   - WebSockets         (Markus Sattler - arduinoWebSockets)
 */

#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── WiFi ──────────────────────────────────────────────────
const char* WIFI_SSID     = "Wifi_Office 5GHz";
const char* WIFI_PASSWORD = "5tgb@1234567890";

// ─── RS485 Modbus ─────────────────────────────────────────
#define RXD_PIN   16
#define TXD_PIN   17
#define DE_PIN    18
#define RE_PIN    19
#define MODBUS_BAUD 9600
#define SLAVE_ID    1

// ─── LED ──────────────────────────────────────────────────
#define LED_RED    13
#define LED_YELLOW 12
#define LED_GREEN  14

// ─── LCD ──────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ─── Modbus ───────────────────────────────────────────────
ModbusMaster node;

// ─── WebSocket Server port 81 ─────────────────────────────
WebSocketsServer webSocket(81);

// ─── Sensor Data ──────────────────────────────────────────
struct SoilData {
  float moisture;
  float temperature;
  float ec;
  float ph;
  int   nitrogen;
  int   phosphorus;
  int   potassium;
} soil;

// ─── Timing ───────────────────────────────────────────────
unsigned long lastSensorRead  = 0;
unsigned long lastWsbroadcast = 0;
unsigned long lastLcdSwitch   = 0;
const unsigned long SENSOR_INTERVAL   = 1000;  // read every 1s
const unsigned long WS_INTERVAL       = 2000;  // broadcast every 2s
const unsigned long LCD_SWITCH_INTERVAL = 4000; // switch LCD page every 4s
int lcdPage = 0;

// ─── RS485 direction control ──────────────────────────────
void preTransmission()  { digitalWrite(DE_PIN, HIGH); digitalWrite(RE_PIN, HIGH); }
void postTransmission() { digitalWrite(DE_PIN, LOW);  digitalWrite(RE_PIN, LOW);  }

// ─── Read Modbus sensor ───────────────────────────────────
bool readSensor() {
  uint8_t result = node.readHoldingRegisters(0x0000, 7);
  if (result == node.ku8MBSuccess) {
    soil.moisture    = node.getResponseBuffer(0) / 10.0;
    soil.temperature = node.getResponseBuffer(1) / 10.0;
    soil.ec          = node.getResponseBuffer(2);
    soil.ph          = node.getResponseBuffer(3) / 10.0;
    soil.nitrogen    = node.getResponseBuffer(4);
    soil.phosphorus  = node.getResponseBuffer(5);
    soil.potassium   = node.getResponseBuffer(6);
    return true;
  }
  return false;
}

// ─── LED indicator by moisture ───────────────────────────
void updateLED() {
  digitalWrite(LED_RED,    LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN,  LOW);

  if (soil.moisture < 30.0) {
    digitalWrite(LED_RED, HIGH);
  } else if (soil.moisture <= 80.0) {
    digitalWrite(LED_YELLOW, HIGH);
  } else {
    digitalWrite(LED_GREEN, HIGH);
  }
}

// ─── LCD display (2 pages) ────────────────────────────────
void updateLCD() {
  lcd.clear();
  if (lcdPage == 0) {
    // Page 1: Moisture, Temp, EC, pH
    lcd.setCursor(0, 0); lcd.print("Moisture: ");   lcd.print(soil.moisture, 1);   lcd.print("%");
    lcd.setCursor(0, 1); lcd.print("Temp:     ");   lcd.print(soil.temperature, 1); lcd.print(" C");
    lcd.setCursor(0, 2); lcd.print("EC:       ");   lcd.print(soil.ec, 0);          lcd.print(" us/cm");
    lcd.setCursor(0, 3); lcd.print("pH:       ");   lcd.print(soil.ph, 1);
  } else {
    // Page 2: N, P, K + IP
    lcd.setCursor(0, 0); lcd.print("Nitrogen:  "); lcd.print(soil.nitrogen);  lcd.print(" mg/kg");
    lcd.setCursor(0, 1); lcd.print("Phosphorus:"); lcd.print(soil.phosphorus); lcd.print(" mg/kg");
    lcd.setCursor(0, 2); lcd.print("Potassium: "); lcd.print(soil.potassium); lcd.print(" mg/kg");
    lcd.setCursor(0, 3); lcd.print(WiFi.localIP().toString());
  }
}

// ─── Broadcast JSON via WebSocket ────────────────────────
void broadcastSensorData() {
  StaticJsonDocument<256> doc;
  doc["moisture"]    = soil.moisture;
  doc["temperature"] = soil.temperature;
  doc["ec"]          = soil.ec;
  doc["ph"]          = soil.ph;
  doc["nitrogen"]    = soil.nitrogen;
  doc["phosphorus"]  = soil.phosphorus;
  doc["potassium"]   = soil.potassium;

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

// ─── WebSocket event handler ──────────────────────────────
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[WS] Client #%u connected\n", num);
      broadcastSensorData(); // send immediately on connect
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client #%u disconnected\n", num);
      break;
    default:
      break;
  }
}

// ─── WiFi connect ─────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Connecting WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print("IP:");
    lcd.setCursor(0, 2); lcd.print(WiFi.localIP().toString());
    lcd.setCursor(0, 3); lcd.print("WS Port: 81");
    delay(3000);
  } else {
    Serial.println("\nWiFi FAILED — running offline");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi FAILED");
    lcd.setCursor(0, 1); lcd.print("Running offline");
    delay(2000);
  }
}

// ─── Setup ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // LED
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);

  // LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Soil Sensor 7-in-1");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  // RS485 Modbus
  Serial2.begin(MODBUS_BAUD, SERIAL_8N1, RXD_PIN, TXD_PIN);
  pinMode(DE_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  digitalWrite(DE_PIN, LOW);
  digitalWrite(RE_PIN, LOW);

  node.begin(SLAVE_ID, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // WiFi
  connectWiFi();

  // WebSocket
  if (WiFi.status() == WL_CONNECTED) {
    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);
    Serial.println("WebSocket server started on port 81");
  }

  delay(500);
}

// ─── Loop ────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // WebSocket loop
  if (WiFi.status() == WL_CONNECTED) {
    webSocket.loop();
  }

  // Read sensor every 1s
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    if (readSensor()) {
      updateLED();
    }
  }

  // Broadcast WebSocket every 2s
  if (now - lastWsbroadcast >= WS_INTERVAL) {
    lastWsbroadcast = now;
    if (WiFi.status() == WL_CONNECTED) {
      broadcastSensorData();
    }
  }

  // LCD page switch every 4s
  if (now - lastLcdSwitch >= LCD_SWITCH_INTERVAL) {
    lastLcdSwitch = now;
    lcdPage = (lcdPage + 1) % 2;
    updateLCD();
  }

  // WiFi reconnect if lost
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (now - lastReconnect > 10000) {
      lastReconnect = now;
      Serial.println("WiFi lost — reconnecting...");
      WiFi.reconnect();
    }
  }
}
