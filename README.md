# ESP32-S3 Lebensmittel BT Scanner

Ein vollständiger IoT-Scanner für Lebensmittel-Barcodes mit **Waveshare ESP32-S3-Touch-LCD-3.5** Board und LVGL GUI.

## Board: Waveshare ESP32-S3-Touch-LCD-3.5

### Hardware-Spezifikationen

- **Prozessor**: ESP32-S3 Dual-Core LX7 @ 240 MHz
- **RAM**: 8 MB PSRAM
- **Flash**: 16 MB
- **Display**: 3.5" IPS Touch (320×480) - ST7796 Controller
- **Touch**: FT6336 I2C Touch Controller
- **Audio**: I2S Audio Codec (Lautsprecher)
- **Sensoren**: QMI8658 IMU, PCF85063 RTC
- **Power**: AXP2101 PMIC
- **Speicher**: TF-Card Slot
- **Kamera**: Connector vorhanden
- **Konnektivität**: USB-C, WiFi 802.11 b/g/n, Bluetooth LE

## Features

✅ **LVGL GUI** - 320×480 Touch-Display Benutzeroberfläche  
✅ **Touch-Support** - FT6336 Touch Input Handling  
✅ **Audio/Lautsprecher** - I2S Audio Output mit Tone-Generator  
✅ **WiFi Management** - Access Point Mode + Station Mode  
✅ **Persistent Storage** - Credentials in Preferences speichern  
✅ **Multi-Task** - FreeRTOS für parallele Verarbeitung  

## Softwarearchitektur

```
src/
├── main.cpp              - Hauptprogramm + FreeRTOS Tasks
├── display.cpp           - ST7796 Display & LVGL Integration
├── touch.cpp             - FT6336 Touch Input
├── audio.cpp             - I2S Audio Output
└── wifi_manager.cpp      - WiFi AP + Station Mode

include/
├── config.h              - GPIO Definitionen & Konfiguration
├── display.h
├── touch.h
├── audio.h
├── wifi_manager.h
└── ui.h                  - LVGL Screen Definitions

lv_conf.h                 - LVGL Configuration
platformio.ini            - PlatformIO Project Setup
```

## GPIO-Belegung

### Display (ST7796 SPI) - **BELEGT**
```
LCD_MOSI (11)  LCD_CLK (12)  LCD_CS (10)
LCD_DC (13)    LCD_RST (9)   LCD_BL (46)
```

### Touch (FT6336 I2C) - **BELEGT**
```
SDA (8)  SCL (7)  INT (4)
```

### Audio (I2S) - **BELEGT**
```
BCLK (41)  LRCK (42)  DOUT (45)  DIN (2)
```

### UART (TTL/Scanner) - **VERFÜGBAR**
```
TX (43)  RX (44)  ← Perfekt für Barcode-Scanner/Drucker
```

### Frei und nutzbar
```
GPIO 35, 36, 37  ← Realistisch frei
GPIO 1, 14       ← Teilweise nutzbar (SD-Konflikt)
```

## Installation & Setup

### Voraussetzungen
- PlatformIO Core oder VS Code + PlatformIO Extension
- ESP32 Support in Arduino IDE

### Schnelstart

```bash
# Clone & navigate
git clone <repo>
cd ESP32_Lebensmittel_BT_Scanner

# Kompilieren
platformio run

# Upload zum Board
platformio run -t upload

# Serial Monitor
platformio device monitor -b 115200
```

## Verwendung

### Serielles Interface

```
Befehle über Serial Monitor (115200 baud):

scan        → WiFi-Netzwerke scannen
ap          → Access Point mit SSID "ESP32-Scanner" starten
connect     → Mit WiFi verbinden
status      → Systemstatus anzeigen
beep        → Test-Ton (1kHz) abspielen
help        → Kommando-Hilfe
```

### API-Beispiele

```cpp
// Access Point starten
wifi_manager.startAP("MyNetwork", "password123");

// Mit WiFi verbinden
wifi_manager.connectToWiFi("HomeWiFi", "password");

// Audio abspielen - 1kHz Ton für 200ms
audio_obj.playTone(1000, 200);

// Touch lesen
TouchPoint point = touch_obj.read();
if (point.pressed) {
    Serial.printf("Touch at X:%d Y:%d\n", point.x, point.y);
}

// WiFi Status
Serial.println(wifi_manager.isConnected() ? "Online" : "Offline");
Serial.println(wifi_manager.getIPAddress());
```

## FreeRTOS Tasks

| Task | Core | Prio | Funktion |
|------|------|------|----------|
| LVGL Update | 0 | 2 | Display Refresh (~10ms) |
| Touch Read | 1 | 3 | Input Polling (~50ms) |
| WiFi Update | 1 | 1 | Status Check (~5s) |

## Hardware-Konfiguration

Alle GPIOs sind in `include/config.h` definiert:

```cpp
// Display
#define LCD_MOSI    11
#define LCD_CLK     12
// ... weitere Pins

// Touch
#define TOUCH_SDA   8
#define TOUCH_SCL   7
// ... etc
```

Pins anpassen falls nötig - Datei bearbeiten + neu compilieren.

## Erweiterungsmöglichkeiten

- [ ] **Barcode-Scanner Integration** (UART43/44)
- [ ] **SD-Card FAT32** (SPI belegt)
- [ ] **Bluetooth LE** (Stack vorhanden)
- [ ] **Datenbank/SPIFFS** für Lebensmittel-Katalog
- [ ] **Kamera-Modul** (DVP-Interface)
- [ ] **Cloud-Sync** über WiFi
- [ ] **Batterie-Management** (AXP2101 I2C)

## Bekannte Einschränkungen

**GPIO-Mangel**: Das Board hat viele peripherale Geräte (Display, Touch, Audio, RTC, IMU). Nur wenige freie Pins!

**Lösungsansätze**:
- UART (43/44) für externe Geräte
- I2C-Bus erweitern (7/8) - vorsicht vor Konflikten
- Bluetooth LE nutzen für Verbindungen
- Optional: SD-SPI teilt Pins mit Display

## Debugging

```cpp
// Serielles Output aktiviert
Serial.println("[Tag] Message");

// LVGL Debug Level in lv_conf.h
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
```

## Lizenz

MIT

## Support

Probleme oder Feature-Wünsche? GitHub Issues erstellen!