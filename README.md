# ESP32-S3 Lebensmittel BT Scanner

Modularer Projektstart für einen **Waveshare ESP32-S3-Touch-LCD-3.5** Lebensmittel-Scanner mit Touch-UI, Barcode-Scanner, ESC/POS-Drucker, Inventarverwaltung und Webinterface.

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
src/main.cpp                 - minimaler Einstiegspunkt
src/core/                    - App-Lifecycle, State Machine, EventBus, Logger
src/models/                  - InventoryItem, ProductInfo, Kategorien, Statistiken
src/storage/                 - LittleFS, JSON, NVS
src/inventory/               - Inventar-Persistenz und Label-Zähler
src/scanner/                 - UART/BLE Scanner-Abstraktion
src/printer/                 - ESC/POS und Label-Rendering
src/network/                 - API, Open Food Facts, Sync, MQTT, Telegram, OTA
src/web/                     - lokale SPA und JSON-API
src/display.cpp, touch.cpp   - aktuell stabile Hardware-Treiber
include/config.h             - Board-Pins und Konstanten
docs/ARCHITECTURE.md         - Architektur, Datenfluss und Ausbauplan
```

## Features

✅ Stabiler TFT_eSPI Display-Start für Waveshare ESP32-S3-Touch-LCD-3.5
✅ Modularer App-State-Machine-Start
✅ Touch-, Audio- und WiFi-Basistreiber
✅ Scanner-, Printer-, Inventory-, Storage-, Network- und Web-Scaffolding
✅ LittleFS/NVS Speicherstrategie vorbereitet
✅ Architektur-Dokumentation für den produktionsreifen Ausbau

## GPIO

| Funktion | GPIO |
|----------|------|
| Display (SPI) | MOSI 1, MISO 2, DC 3, SCLK 5, BL 6 |
| Display via TCA9554 | RST (expander), CS not direct GPIO |
| Touch (I2C) | SDA 8, SCL 7, INT via TCA9554 |
| Audio (ES8311/I2S) | MCLK 12, BCLK 13, DOUT 14, LRCK 15, DIN 16 |
| SD (SPI) | MISO 9, MOSI 10, SCLK 11, CS via TCA9554 |
| UART | TX 43, RX 44 |
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
- TFT_eSPI 2.5.43+
- LVGL ist aktuell noch nicht eingebunden; die Firmware rendert direkt mit TFT_eSPI.

## Lizenz

MIT
