# Lebensmittel-Scanner Firmware Architektur

Dieses Projekt ist als modularer Startpunkt für eine produktionsreife ESP32-S3-Firmware aufgebaut. Die bisher funktionierende Display-/Pin-Konfiguration bleibt erhalten, `main.cpp` delegiert aber nur noch an die App-Schicht.

## Zielzustand

* Querformat-Touch-UI für 480×320 bzw. 456×280.
* Barcode-Einlagerung per UART-Scanner oder BLE-HID.
* ESC/POS-Thermodrucker mit QR-Etiketten.
* Lokales Inventar in LittleFS mit atomaren JSON-Schreibvorgängen.
* Persistente Zähler/Konfigurationen in NVS.
* Open-Food-Facts-Produktdaten mit lokalem Cache.
* Webinterface ohne externe CDNs.
* Spätere Integrationen: MQTT, Telegram, Server-Sync, OTA.

## Schichten

| Schicht | Verzeichnis | Aufgabe |
|---|---|---|
| Core | `src/core` | App-Lifecycle, Zustandsautomat, EventBus, Logging, Zeit |
| UI/Hardware | `src/display.cpp`, `src/touch.cpp`, später `src/ui` | TFT, Touch, Screens, Theme, Eingaben |
| Scanner | `src/scanner` | UART/BLE-Abstraktion und Code-Klassifikation |
| Printer | `src/printer` | ESC/POS-Basis, Label-Renderer, Druckmanager |
| Inventory | `src/inventory` | Inventar, Label-Zähler, JSON-Persistenz |
| Storage | `src/storage` | LittleFS/NVS/JSON-Hilfen mit Recovery |
| Network | `src/network` | API, Open Food Facts, Sync, MQTT, Telegram, OTA |
| Web | `src/web` | Lokale SPA und JSON-API-Routen |
| Models | `src/models` | Datenmodelle ohne Logik |

## Zustandsautomat

Der zentrale State liegt in `AppStateManager`:

`BOOTING → WIFI_CONNECTING → AP_MODE|MAIN`

Danach werden Workflows über Events fortgeführt:

* `BARCODE_SCANNED` → `FETCHING` → `ENTER_DATE` → `ENTER_QTY` → `SAVING` → `PRINTING` → `SUCCESS`
* `LABEL_SCANNED` → `RETRIEVE`
* Inventar-Button → `INVENTORY_BROWSE`
* Swipe-Down → `HOUSEHOLD_SELECT` → `HOUSEHOLD_INVENTORY`
* Timeout → `POWER_SAVE`

## Speicherstrategie

LittleFS-Dateien:

* `/inventory.json`
* `/custom_products.json`
* `/categories.json`
* `/off_cache.json`
* `/storage_stats.json`
* `/shopping_list.json`

`JsonStorage` liest JSON robust ein, setzt bei beschädigten Dateien auf Fallback-JSON zurück und schreibt atomar über `*.tmp` + `rename`.

NVS-Namespaces:

* `lager`: Labelzähler `cnt`
* `wifi`: WLAN-Zugangsdaten
* `uicfg`, `fonts`, `printer`, `dev`, `mqtt`, `telegram`, `server`: spätere Konfigurationen

## Rendering-Konzept

Die aktuelle TFT_eSPI-Basis bleibt bewusst konservativ:

* ST7796 SPI über HSPI.
* Querformat wird im nächsten UI-Schritt über `DisplayManager` zentral gesetzt.
* Built-in-Fonts statt optionaler Smooth/FreeFont-Pfade im Bootpfad.
* Später: LVGL mit Double Buffering in PSRAM, Dirty-Area-Flushing und Touch-Input-Driver.

## Webinterface

`WebInterface` ist als lokaler SPA-Startpunkt angelegt:

* `/` liefert eingebettetes HTML/CSS/JS ohne CDN.
* `/api/system-info` liefert JSON.
* Die geplanten API-Gruppen sind Inventar, Vorlagen, Kategorien, Design, WLAN, MQTT, Telegram, Sync, OTA, Scanner, Drucker und Statistik.

## Nächste Implementierungsschritte

1. `App` um echte Eventverarbeitung für Scan → Fetch → Save → Print erweitern.
2. `LittleFSManager` in `App::begin()` starten und `InventoryManager` verdrahten.
3. Open-Food-Facts-LRU-Cache (`/off_cache.json`, max. 50 Einträge) hinzufügen.
4. Web-API-Routen für Inventar und Konfiguration ergänzen.
5. LVGL-DisplayManager/ScreenManager als Ersatz für direkte TFT-Statusseiten einführen.
6. NimBLE-HID-Scanner implementieren.
7. MQTT/Telegram/Server-Sync als nicht-blockierende Queue-Worker aktivieren.
