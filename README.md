# ESP32-S3 Lebensmittel BT Scanner

Basis-Code für **Waveshare ESP32-S3-Touch-LCD-3.5** mit LVGL GUI, Touch, Audio, WiFi.

## Hardware

- ESP32-S3 (240 MHz, 16MB Flash, 8MB PSRAM)
- 3.5" IPS Touch Display (ST7796)
- FT6336 Touch Controller (I2C)
- I2S Audio Output
- WiFi + Bluetooth LE

## Quick Start

```bash
# Build
platformio run

# Upload
platformio run -t upload

# Monitor
platformio device monitor -b 115200
```

## Struktur

```
include/        - Header Dateien
├── config.h    - GPIO Definitionen
├── display.h   - Display Manager
├── touch.h     - Touch Input
├── audio.h     - Audio/Speaker
├── wifi_manager.h - WiFi AP/Station
└── ui.h        - LVGL UI

src/            - Implementierung
├── main.cpp    - Hauptprogramm + FreeRTOS
├── display.cpp
├── touch.cpp
├── audio.cpp
└── wifi_manager.cpp

lv_conf.h       - LVGL Config
platformio.ini  - PlatformIO Setup
```

## Features

✅ LVGL GUI (320×480)  
✅ Touch Input  
✅ Audio Output  
✅ WiFi (AP + Station)  
✅ FreeRTOS Multi-Core  
✅ Persistent Storage  

## GPIO

| Funktion | GPIO |
|----------|------|
| Display (SPI) | 9-13, 46 |
| Touch (I2C) | 7, 8, 4 |
| Audio (I2S) | 2, 41-42, 45 |
| UART | 43, 44 |
| Frei | 35, 36, 37 |

## Serial Commands

```
scan        - WiFi scan
ap          - Start AP Mode
status      - System status
beep        - Test tone
help        - Help
```

## Requirements

- PlatformIO
- ESP32 Arduino Framework
- LVGL 9.2.0+
- TFT_eSPI 2.6.0+

## Lizenz

MIT