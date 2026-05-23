# ESP32 Lebensmittel-Scanner – Projektdokumentation für Claude

## Projektübersicht

Lebensmittel-Inventarsystem auf Basis eines ESP32-S3 (N16R8, 16 MB Flash, PSRAM).
Kernfunktionen: Barcode-Scannen via BLE-Scanner, Touchscreen-UI, Etikettendruck,
Netzwerk-Sync (MySQL/MQTT) und Web-Interface für Desktop & Mobil.

**Hardware**
- Board: Waveshare ESP32-S3-Touch-LCD-3.5 (ST7796 SPI, FT6336 Capacitive Touch)
- Display: 480×320, Querformat, SPI über HSPI (MOSI=1, MISO=2, SCLK=5, DC=3, BL=6)
- BLE-Scanner: Wireless-Barcodescanner, verbindet sich als HID-Gerät + Battery Service
- Drucker: ESC/POS Thermodrucker (Etiketten mit QR-Code + MHD)

---

## Build & Deployment

### Toolchain
```
Platform: pioarduino/platform-espressif32 55.03.38-1 (ESP-IDF 5.3.x, Arduino 3.x)
Framework: Arduino
Filesystem: LittleFS  (board_build.filesystem = littlefs)
Partition:  partitions_16mb_dual_fs.csv
```

### Partitionstabelle
| Partition | Typ  | Größe  | Zweck                              |
|-----------|------|--------|------------------------------------|
| nvs       | data | 20 KB  | Konfiguration / Schlüssel          |
| otadata   | data | 8 KB   | OTA-Boot-Selektor                  |
| app0      | app  | 6.5 MB | Firmware Slot A                    |
| app1      | app  | 6.5 MB | Firmware Slot B (OTA-Ziel)         |
| spiffs    | data | 1 MB   | LittleFS – Web-Dateien             |
| userdata  | data | 2.25 MB| LittleFS – Inventar, Konfig, JSON |
| coredump  | data | 64 KB  | Crash-Dump                         |

### Bauen & Flashen (seriell)
```bash
pio run -e esp32-s3-devkitc1-n16r8 --target upload     # Firmware + FS (auto)
pio run -e esp32-s3-devkitc1-n16r8 --target uploadfs   # nur Filesystem
pio run -t buildfs                                       # nur littlefs.bin bauen
```

Das Post-Build-Skript `scripts/auto_uploadfs.py` vergleicht einen SHA-256-Hash von `data/`
mit dem letzten Upload. Das Filesystem wird **nur bei Änderungen** neu geflasht.

### OTA (kabellos)
Der `/api/update`-Endpunkt **erkennt den Typ automatisch** am ersten Byte:
- `0xE9` → ESP32-Firmware-Magic → flashed `app1` (Firmware)
- alles andere → LittleFS-Image → flashed `spiffs`-Partition

Ablauf für vollständiges Update ohne USB:
1. `firmware.bin` über Web-UI → OTA Update → Manuell → "Firmware (.bin)" hochladen
2. `littlefs.bin` über **dasselbe Formular** hochladen (Auto-Erkennung greift)

---

## Architektur

### Verzeichnisstruktur
```
src/
  core/          App.cpp/h, AppStateManager, Logger, DeviceConfig, TimeManager
  scanner/       BLEScanner (NimBLE HID + Battery), BarcodeManager
  inventory/     InventoryManager, InventoryStorage, LabelCounter
  storage/       AppFS, JsonStorage, LittleFSManager, BackupManager
  network/       ApiClient, SyncManager, OpenFoodFacts, NtfyNotifier
  printer/       PrinterManager (ESC/POS)
  web/           WebInterface.cpp (alle HTTP-Routen)
  models/        ProductInfo, InventoryItem, ProductTemplate
include/
  config.h       GPIO-Pins, Konstanten
  display.h      showHome(), showResult() etc. Signaturen
  FreeSans22pt.h Auto-generierter GFX-Font (ASCII + Latin-1)
data/            Web-Assets (index.html, mobile.html, app.js, style.css, …)
scripts/
  version.py          Pre-Build: generiert Versions-Header
  auto_uploadfs.py    Post-Build: hashed data/ und uploaded FS bei Änderung
  gen_gfx_font.py     Generiert FreeSans22pt.h aus TTF (einmalig ausführen)
```

### Hauptschleife & Tasks
- **Core 1**: `App::loop()` – Workflow-State-Machine, UI, Timers
- **Core 0**: Touch-Polling-Task (gestartet in `display_obj.startTouchTask()`)
- **FreeRTOS-Task**: Produkt-Fetch via API (`fetchTaskFn`) – asynchron
- **FreeRTOS-Task**: BLE-Scanner-Verbindung (`connectTask` in BLEScanner)

### State Machine (`WorkflowMode`)
Kern des Scanvorgangs in `App.cpp`. Zustände u.a.:
`HOME → FETCHING_PRODUCT → ENTER_DATE → ENTER_QTY → SAVING → RESULT`
`HOME → TMPL_CATEGORY → TMPL_PRODUCT → TMPL_BRAND → TMPL_SORTE → TMPL_AMOUNT → SAVING`

### Display-Rendering
- Alle Zeichenoperationen gehen in einen PSRAM-Sprite (`TFT_eSprite _spr`, 480×320×16bit)
- `commit()` pusht den Sprite per `pushSprite(0,0)` auf das Display
- Schriftarten: TFT_eSPI Bitmap-Fonts (Font 2, 4) + GFX-Font `FreeSans22pt` für Kategorie-Kacheln

---

## Wichtige Implementierungsdetails

### TFT_eSPI & HSPI
```
-DUSE_HSPI_PORT=1
```
Der Default-FSPI-Pfad des ESP32-S3 dereferenziert eine ungültige SPI-Register-Basis
→ Panic mit `EXCVADDR=0x10`. Immer HSPI erzwingen.

### Deutsche Umlaute / ß auf dem Display
TFT_eSPI Bitmap-Fonts (Font 2, 4 etc.) unterstützen nur ASCII 0x20–0x7E.
Für Kategorie-Kacheln wird `setFreeFont(&FreeSans22pt)` verwendet:
- Font-Header `include/FreeSans22pt.h` deckt 0x20–0xFF (Latin-1) ab
- Generiert mit `python3 scripts/gen_gfx_font.py FreeSans.ttf 22 FreeSans22pt include/FreeSans22pt.h`
- **UTF-8 direkt übergeben** – TFT_eSPI dekodiert intern. Kein Latin-1-Konvert!
  (0xDF = ß in Latin-1 wird als 2-Byte-UTF8-Start missinterpretiert → Zeichen fehlt)
- Nach dem drawString mit FreeFont immer `setTextFont(4)` zurücksetzen

### SD-Karten-Logging
`Logger::log()` schreibt **nur WARN und ERROR** auf SD (`/scanner_log/YYYY-MM-DD.log`).
INFO/DEBUG würden bei jedem Render-Zyklus SD open/write/close verursachen
→ WDT-Timeout / Absturz beim Seitenwechsel.

7-Tage-Rotation: `Logger::pruneOldLogs()` löscht älteste Dateien.
Web-Download: `GET /api/logs/sd` (Liste), `GET /api/logs/sd/DATEI.log` (Download).

### Zurückbuchen via Re-Scan
Wird ein Artikel ausgelagert, landet er im 48h-`_recentlyRemoved`-Puffer.
Scannt man denselben **LebNumber-Label-Barcode** erneut, wird er automatisch
mit dem aktuell aktiven Lagerort wieder eingelagert (`InventoryManager::restoreByLabel()`).
Kein zusätzlicher Screen – das Verhalten ist transparent.

### BLE Scanner-Akkustand
BLE Battery Service (UUID 0x180F / Char 0x2A19) wird nach HID-Verbindung abonniert.
Polling alle 5 Minuten via `ble_scanner.readBatteryNow()`.
Bei < 10%: Warnton + Statusmeldung auf Display + farbige Anzeige im Web-UI.

### Zirkuläre Abhängigkeit Logger ↔ AppFS
`Logger.cpp` darf `AppFS.h` nicht inkludieren (AppFS.cpp inkludiert Logger.h).
Lösung: `Logger::enableSdLog(fs::FS *sdFs)` erhält einen rohen `fs::FS*`-Zeiger
statt intern `AppFS::sdFs()` aufzurufen.

---

## Web-Interface

### Admin-UI (`/` → `index.html` + `app.js`)
Vollständige Verwaltung über Browser:
- Inventar, Lagerorte, Kategorien, Vorlagen (CRUD)
- OTA Update (Firmware + Filesystem, Auto-Erkennung)
- System-Logs (RAM-Ring + SD-Download)
- Display-/Netzwerk-Konfiguration

### Mobile PWA (`/mobile` → `mobile.html`)
iOS/Android-optimierte Ansicht:
- **Swipe-to-delete**: `.swipe-inner` hat `position:relative; z-index:1` damit der
  Löschen-Button dahinter liegt und erst beim Links-Wischen sichtbar wird
- **iOS Safe Area**: `viewport-fit=cover` + `env(safe-area-inset-bottom)` auf Tab-Bar
- 4 Tabs: Inventar, Ablaufend, Vorlagen, System

### Datumsformat-Konvention
- Firmware speichert MHD als `YYYY-MM-DD` (ISO, template workflow) oder `DD.MM.YYYY` (manuell)
- Web-UI normalisiert mit `toIsoDate(str)` in `app.js` vor Anzeige und Speicherung
- HTML `<input type="date">` erfordert `YYYY-MM-DD`

---

## Bekannte Fallstricke

| Problem | Lösung |
|---------|--------|
| SPI Panic EXCVADDR=0x10 | `-DUSE_HSPI_PORT=1` in platformio.ini |
| ß fehlt auf Display | UTF-8-String direkt an `drawString()` – KEIN Latin-1-Konvert |
| WDT-Absturz bei SD-Log | Nur WARN/ERROR auf SD schreiben |
| OTA Filesystem schlägt fehl | `/api/update` erkennt Typ automatisch; LittleFS wird vor dem Schreiben gemountet |
| `saveJson` not declared | `static bool saveJson(...)` Forward-Declaration vor `WebInterface::begin()` |
| Inventar-Löschen/-Bearbeiten defekt | `data-lb`-Attribut und Solo-Artikel-Pfad korrigiert |

---

## Entwicklungsregeln

- **Branch**: Immer direkt auf `main` arbeiten und committen
- **Releases**: Vor größeren Features einen GitHub-Release erstellen
- **Commits**: Auf Deutsch, präziser Betreff, Begründung im Body
- **Kein `--no-verify`**, kein Force-Push auf main
- **Logs**: Neue `Logger::info()`-Aufrufe sparsam einsetzen (SD-Impact beachten)
- **Fonts**: Nur `setFreeFont()` in `showCategoryTiles()`, überall sonst `setTextFont()`
