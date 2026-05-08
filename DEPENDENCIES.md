# Abhängigkeiten und Versionen

Dieses Projekt verwendet nur **aktuelle stabile Versionen** aller Bibliotheken (Stand Mai 2026).

## Installierte Versionen

### Core Platform
- **PlatformIO**: 6.8.1 (Espressif32)
- **Arduino Framework**: Latest stable (ESP32)
- **ESP32 Arduino Core**: 3.0.x (latest stable)

### Display & Graphics
| Bibliothek | Version | Zweck |
|-----------|---------|-------|
| **LVGL** | 9.2.0+ | UI Framework für Touch-Display |
| **TFT_eSPI** | 2.6.0+ | ST7796 Display-Treiber |

### Kommunikation & Netzwerk
| Bibliothek | Version | Zweck |
|-----------|---------|-------|
| **ESP Async WebServer** | 1.2.7+ | Web Server für AP-Mode |
| **AsyncTCP** | 1.1.4+ | Async TCP Stack |
| **ArduinoJson** | 7.1.0+ | JSON Parsing |

### System & Tools
| Bibliothek | Version | Zweck |
|-----------|---------|-------|
| **Wire** | Built-in | I2C für Touch/IMU/RTC |
| **SPI** | Built-in | SPI für Display/SD |
| **I2S Driver** | Built-in | Audio Output |
| **Preferences** | Built-in | NVS Storage |

## Warum Stable Versions?

✅ **Getesteter Code**: Stabile Versionen sind gründlich getestet  
✅ **Weniger Bugs**: Produktionscode ohne experimentelle Features  
✅ **Langzeitunterstützung**: Längere Update-Zyklen  
✅ **Breaking-Change-free**: Kompatibilität über mehrere Minor-Versionen  
✅ **Performance**: Optimiert für Production Use  

## Library Versioning Schema

```
LVGL:           9.2.0 (Major.Minor.Patch)
TFT_eSPI:       2.6.0 (Major.Minor.Patch)
ArduinoJson:    7.1.0 (Major.Minor.Patch)
ESP Async Web:  1.2.7 (Major.Minor.Patch)
```

## Update-Strategie

### Regelmäßig Prüfen
```bash
platformio lib update
```

### Sichere Updates
- Nur **Patch-Versionen** automatisch updaten (z.B. 9.2.0 → 9.2.1)
- **Minor-Updates** vor Release testen (z.B. 9.2.0 → 9.3.0)
- **Major-Updates** nur mit Code-Review (z.B. 9.x → 10.x)

## Known Compatibility

| Komponente | Getestet Mit | Status |
|-----------|-----------|--------|
| LVGL 9.x | TFT_eSPI 2.6.0 | ✅ Kompatibel |
| ESP32-S3 | ESP32 Arduino 3.0.x | ✅ Kompatibel |
| FT6336 Touch | I2C Wire | ✅ Kompatibel |
| I2S Audio | ESP32 I2S Driver | ✅ Kompatibel |

## Fehlerbehebung

### "LVGL version mismatch"
→ Stelle sicher, dass `lv_conf.h` mit LVGL-Version kompatibel ist

### "TFT_eSPI compilation error"
→ `User_Setup.h` in include/ muss vorhanden sein

### "AsyncWebServer port already in use"
→ WiFi AP-Port 80 ist in Verwendung

## Changelog

### Version 1.0.0 (Mai 2026)
- ✅ LVGL 9.2.0
- ✅ TFT_eSPI 2.6.0
- ✅ ESP Async WebServer 1.2.7
- ✅ PlatformIO 6.8.1

Alle Libraries sind stabil, getestet und produktionsreif.
