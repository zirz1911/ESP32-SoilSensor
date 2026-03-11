# ESP32 Soil Sensor 7-in-1 🌱

Real-time soil monitoring system with RS485 Modbus, LCD display, LED indicators, WiFi, WebSocket, and a live web dashboard.

---

## Features

- **7 soil parameters** — Moisture, Temperature, EC, pH, Nitrogen, Phosphorus, Potassium
- **RS485 Modbus RTU** — raw Serial2 communication at 4800 baud
- **LCD 20×4 I2C** — 3-page auto-rotating display
- **LED indicators** — moisture status at a glance
- **WiFi + WebSocket** — broadcasts JSON every 2 seconds on port 81
- **Web Dashboard** — single HTML file, dark theme, live-updating cards
- **Auto-reconnect** — WiFi and WebSocket both recover automatically

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32 (any variant) |
| Sensor | 7-in-1 Soil Sensor (RS485 Modbus RTU) |
| Display | LCD 20×4 I2C @ address `0x27` |
| LED Red | GPIO 13 — Moisture < 30% |
| LED Yellow | GPIO 12 — Moisture 31–80% |
| LED Green | GPIO 14 — Moisture > 80% |

### RS485 Wiring

| ESP32 Pin | RS485 Module |
|-----------|-------------|
| GPIO 16 (RX2) | RO |
| GPIO 17 (TX2) | DI |
| GPIO 18 | DE |
| GPIO 19 | RE |
| GND | GND |
| 5V | VCC |

### LCD Wiring (I2C)

| ESP32 Pin | LCD |
|-----------|-----|
| GPIO 21 (SDA) | SDA |
| GPIO 22 (SCL) | SCL |
| 5V | VCC |
| GND | GND |

---

## Wiring Diagram

```
ESP32                RS485 Module          Soil Sensor
─────                ────────────          ───────────
GPIO16 (RX2) ───── RO                A ─── A (RS485+)
GPIO17 (TX2) ───── DI                B ─── B (RS485-)
GPIO18       ───── DE
GPIO19       ───── RE
5V           ───── VCC
GND          ───── GND

ESP32                LCD 20x4 I2C
─────                ────────────
GPIO21 (SDA) ─────── SDA
GPIO22 (SCL) ─────── SCL
5V           ─────── VCC
GND          ─────── GND

ESP32                LEDs (with 220Ω resistor)
─────                ──────────────────────────
GPIO13 ─── 🔴 Red     (Dry: moisture < 30%)
GPIO12 ─── 🟡 Yellow  (OK:  moisture 31–80%)
GPIO14 ─── 🟢 Green   (Wet: moisture > 80%)
GND    ─── Cathode
```

---

## Software Setup

### Arduino Libraries

Install via **Arduino IDE → Library Manager**:

| Library | Author |
|---------|--------|
| `LiquidCrystal_I2C` | Frank de Brabander |
| `WebSockets` | Markus Sattler |
| `ArduinoJson` | Benoit Blanchon |

### Board Setup

1. Install **ESP32 board package** in Arduino IDE
   - Boards Manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Select board: **ESP32 Dev Module**
3. Upload speed: **115200**

### WiFi Configuration

Edit `SoilSensor.ino`:

```cpp
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";
```

---

## Upload & Run

1. Open `SoilSensor.ino` in Arduino IDE
2. Set WiFi credentials
3. Select correct COM port
4. Click **Upload**
5. Open **Serial Monitor** at 115200 baud
6. Note the IP address printed after WiFi connects

---

## LCD Pages

The display rotates automatically every 4 seconds:

**Page 1 — Sensor values**
```
Moist : 45.2 %
Temp  : 28.5 C
EC    : 320 us/cm
pH    : 6.8
```

**Page 2 — NPK**
```
-- NPK (mg/kg) --
N : 45
P : 32
K : 67
```

**Page 3 — WiFi info**
```
-- WiFi Info --
IP:
192.168.1.105
WS Port: 81
```

---

## WebSocket Data

ESP32 broadcasts JSON every **2 seconds** on port `81`:

```json
{
  "moisture": 45.2,
  "temperature": 28.5,
  "ec": 320,
  "ph": 6.8,
  "nitrogen": 45,
  "phosphorus": 32,
  "potassium": 67
}
```

Connect from any WebSocket client:
```
ws://192.168.1.105:81
```

---

## Web Dashboard

Open `dashboard.html` in any browser on the same network.

1. Enter the ESP32 IP address in the input field
2. Click **Connect**
3. Data updates live every 2 seconds

**Dashboard cards:**

| Parameter | Visual | Color |
|-----------|--------|-------|
| Moisture | Progress bar | Blue |
| Temperature | Semicircle gauge | Orange |
| EC | Progress bar | Purple |
| pH | Gradient scale + marker | Green |
| Nitrogen | Progress bar | Lime |
| Phosphorus | Progress bar | Yellow |
| Potassium | Progress bar | Red |

> **Demo mode** — when not connected, the dashboard shows simulated values so you can preview the UI without hardware.

---

## LED Status

| LED | Color | Condition |
|-----|-------|-----------|
| 🔴 Red | ON | Moisture < 30% (Dry — needs water) |
| 🟡 Yellow | ON | Moisture 31–80% (Optimal range) |
| 🟢 Green | ON | Moisture > 80% (Wet) |

---

## Project Structure

```
ESP32-SoilSensor/
├── SoilSensor.ino   # ESP32 firmware (Arduino/C++)
└── dashboard.html   # Web dashboard (single file, no dependencies)
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| No sensor data | Check RS485 wiring A/B polarity — swap if needed |
| LCD blank | Verify I2C address is `0x27` — use I2C scanner sketch |
| WiFi not connecting | Check SSID/password, ensure 2.4GHz or 5GHz matches |
| Dashboard shows "Disconnected" | Confirm ESP32 IP, same network as browser, port 81 not blocked |
| All values zero | Sensor baud rate — default is 4800, some sensors use 9600 |

---

## License

MIT
