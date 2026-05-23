#include "App.h"
#include "Logger.h"
#include "config.h"
#include "audio.h"
#include "wifi_manager.h"
#include "touch.h"
#include "display.h"

#include <WiFi.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <ArduinoJson.h>
#include "../storage/AppFS.h"

App app;

App::App()
    : i2c_bus(0),
      json(fs),
      inventoryStorage(json),
      inventory(inventoryStorage),
      openFoodFacts(api, &fs),
      web(80) {}

void App::begin() {
    // Initialize NVS before anything else.  NimBLE accesses NVS very early
    // (background task at ~1 ms) and can race with the framework's lazy init,
    // causing "nvs_open failed: NOT_INITIALIZED".  Explicit init here wins the race.
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }
    }

    Logger::begin(115200);
    // Suppress ESP-IDF internal log output on UART0 (GPIO43 = printer TX).
    // esp_log uses UART0 by default; only errors are critical enough to keep.
    esp_log_level_set("*", ESP_LOG_ERROR);
    Logger::info("App", "ESP32-S3 Lebensmittel-Scanner booting");
    state.begin(AppState::BOOTING);

    initBacklight();

    // I2C must be up before display init because LCD reset is driven by the
    // TCA9554 I/O expander (address 0x20), not a direct ESP32-S3 GPIO.
    initI2C();
    resetLCDViaTCA9554();

    Logger::info("Display", "Initializing ST7796");
    display_obj.init();
    display_obj.showSplash();

    touch_obj.init(&i2c_bus);
    display_obj.startTouchTask(); // touch polling now runs on Core 0 independently

    Logger::info("Audio", "Initializing audio feedback");
    audio_obj.init(i2c_bus);  // plays startup tone internally

    state.setState(AppState::WIFI_CONNECTING);
    Logger::info("WiFi", "Initializing network manager");
    wifi_manager.init();

    state.setState(AppState::MAIN);

    initFilesystem();
    loadDisplayConfig();
    device_config.begin();
    time_manager.begin(i2c_bus, device_config.getTimezone());
    if (AppFS::sdAvailable())
        Logger::enableSdLog(&AppFS::sdFs());

    // SD backup import prompt: show if SD has backup and internal storage is empty
    if (BackupManager::hasBackupOnSD() && BackupManager::internalIsEmpty()) {
        workflow = WorkflowMode::SD_IMPORT_PROMPT;
        display_obj.showSdImportPrompt(BackupManager::backupDateString());
        // Touch task (Core 0) already calls tick() — just poll hitTest() here.
        // tick() only set _pending_action but hitTest() was never called, so
        // the buttons appeared frozen.
        while (workflow == WorkflowMode::SD_IMPORT_PROMPT) {
            OnscreenAction act = display_obj.hitTest(0, 0);
            if (act == OnscreenAction::SD_IMPORT_YES) {
                BackupManager::doRestore();
                device_config.begin();  // reload restored NVS settings
                workflow = WorkflowMode::HOME;
                Logger::info("Backup", "Import abgeschlossen");
            } else if (act == OnscreenAction::SD_IMPORT_NO) {
                workflow = WorkflowMode::HOME;
            }
            delay(20);
        }
    }
    labelCounter.begin();
    inventory.begin();
    loadManualNames();
    printer.begin();
    sync_manager.begin();

    // Wire MySQL-backed product cache into the OpenFoodFacts lookup chain
    openFoodFacts.setSyncManager(&sync_manager);

    // Boot sync: pull product cache + inventory from MySQL
    if (wifi_manager.isConnected() && sync_manager.hasConfig()) {
        Logger::info("Sync", "Boot: pulling product cache from MySQL...");
        int n = sync_manager.pullProductsToCache(fs);
        if (n >= 0)
            Logger::info("Sync", String("Boot: ") + n + " products pulled from MySQL");
        doInventoryPull();
        _lastInventorySyncMs = millis();
    }

    // Restore active location badge (name + color) on display
    display_obj.setActiveLocation(device_config.getActiveLocation());
    display_obj.setActiveLocationColor(device_config.getActiveLocationColor());

    Serial.flush();
    Logger::info("Scanner", "Initializing BLE barcode scanner");
    Serial.flush();
    barcode_manager.begin();
    renderDashboard("EAN scannen zum Einlagern");

    initWebServer();
    renderDashboard("Bereit");

    Logger::info("App", "Ready. Serial commands: scan, status, beep, print, printplain, printbaud, help");

    // If no WiFi: start async scan and show setup screen on display
    if (!wifi_manager.isConnected()) {
        WiFi.scanNetworks(/*async=*/true, /*hidden=*/true);
        workflow = WorkflowMode::WIFI_SETUP_SCAN;
        display_obj.showWifiScan();
    }
}

void App::loop() {
    // Touch polling runs in its own FreeRTOS task (startTouchTask in begin()).
    // No tick() call needed here.

    // WiFi setup: check async scan result and show network list
    if (workflow == WorkflowMode::WIFI_SETUP_SCAN) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            _wifiNets.clear();
            for (int i = 0; i < n && _wifiNets.size() < 20; i++) {
                String s = WiFi.SSID(i);
                if (s.isEmpty()) continue;
                bool dup = false;
                for (const auto &e : _wifiNets) if (e == s) { dup = true; break; }
                if (!dup) _wifiNets.push_back(s);
            }
            WiFi.scanDelete();
            display_obj.showListScreen("WLAN AUSWAEHLEN", _wifiNets);
            workflow = WorkflowMode::WIFI_SETUP_LIST;
        }
    }

    // WiFi setup: poll for connection result
    if (workflow == WorkflowMode::WIFI_SETUP_CONN) {
        if (wifi_manager.isConnected()) {
            Logger::info("WiFi", "Connected: " + WiFi.localIP().toString());
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::STORE;
            renderDashboard("WLAN verbunden!");
            if (sync_manager.hasConfig()) {
                doInventoryPull();
                _lastInventorySyncMs = millis();
            }
        } else if (millis() - _wifiConnectStartMs > 15000) {
            workflow = WorkflowMode::WIFI_SETUP_PASS;
            _kbText = "";
            display_obj.kbReset();
            display_obj.showKeyboardEntry("PASSWORT (ERNEUT)", "");
        }
    }

    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        handleSerialCommand(command);
    }

    time_manager.loop();
    sync_manager.loop();
    barcode_manager.loop();

    // Periodic inventory pull from MySQL (every 2 minutes, background merge)
    if (sync_manager.hasConfig() && wifi_manager.isConnected()) {
        uint32_t now = millis();
        if (now - _lastInventorySyncMs > INVENTORY_SYNC_INTERVAL_MS) {
            _lastInventorySyncMs = now;
            doInventoryPull();
        }
    }

    // WiFi reconnect watchdog — re-trigger connection if the link silently dropped.
    // setAutoReconnect(true) is unreliable on IDF 5.x; we watch manually.
    {
        bool inSetup = (workflow == WorkflowMode::WIFI_SETUP_SCAN
                     || workflow == WorkflowMode::WIFI_SETUP_LIST
                     || workflow == WorkflowMode::WIFI_SETUP_PASS
                     || workflow == WorkflowMode::WIFI_SETUP_CONFIRM
                     || workflow == WorkflowMode::WIFI_SETUP_CONN);
        uint32_t now = millis();
        if (!inSetup && wifi_manager.hasCredentials()
                && now - _lastWifiCheckMs >= WIFI_CHECK_MS) {
            _lastWifiCheckMs = now;
            if (!wifi_manager.isConnected()) {
                if (_wifiDisconnectedSince == 0) {
                    _wifiDisconnectedSince = now;
                    _wifiLastReconnectMs   = 0;
                    Logger::warn("WiFi", "Verbindung verloren – warte auf Reconnect");
                }
                uint32_t downFor    = now - _wifiDisconnectedSince;
                uint32_t sinceLast  = _wifiLastReconnectMs ? (now - _wifiLastReconnectMs) : UINT32_MAX;
                bool firstTry  = (_wifiLastReconnectMs == 0 && downFor >= WIFI_RECONNECT_MS);
                bool retryDue  = (_wifiLastReconnectMs != 0 && sinceLast >= WIFI_RETRY_MS);
                if (firstTry || retryDue) {
                    _wifiLastReconnectMs = now;
                    Logger::info("WiFi", String("Reconnect-Versuch (down ") + downFor / 1000 + "s)");
                    wifi_manager.reconnect();
                }
            } else if (_wifiDisconnectedSince != 0) {
                Logger::info("WiFi", "Wiederverbunden: " + WiFi.localIP().toString());
                _wifiDisconnectedSince = 0;
                _wifiLastReconnectMs   = 0;
            }
        }
    }
    // Scanner battery — poll every 5 min; warn once when < 10 %
    if (ble_scanner.isConnected()
            && millis() - _lastBatteryPollMs >= BATTERY_POLL_MS) {
        _lastBatteryPollMs = millis();
        ble_scanner.readBatteryNow();
        int bat = ble_scanner.getBatteryLevel();
        if (bat >= 0 && bat < 10 && !_batteryWarnShown) {
            _batteryWarnShown = true;
            audio_obj.playWarningTone();
            Logger::warn("BLE", String("Scanner Akku schwach: ") + bat + "%");
            _statusMessage = "Scanner Akku schwach! " + String(bat) + "%";
            renderActiveTab(_statusMessage);
        } else if (bat >= 10) {
            _batteryWarnShown = false;
        }
    }
    if (!ble_scanner.isConnected()) _batteryWarnShown = false;

    // SD backup — check once per hour, backs up if ≥24 h since last backup
    if (millis() - _lastBackupCheckMs >= BACKUP_CHECK_INTERVAL_MS) {
        _lastBackupCheckMs = millis();
        BackupManager::runDailyCheck();
        Logger::rotateSdLogIfNeeded();  // roll to new day's file if midnight passed
    }

    // Ntfy push check every 6 hours
    if (device_config.getNtfyTopic().length() > 0 &&
        millis() - _lastNtfyCheckMs >= NTFY_CHECK_INTERVAL_MS) {
        _lastNtfyCheckMs = millis();
        int days = device_config.getNtfyDays();
        int expCount = countExpiringSoon(days);
        if (expCount > 0) {
            String title = "Lebensmittel laufen ab!";
            String msg = String(expCount) + " Produkt" + (expCount != 1 ? "e laufen" : " laeuft") +
                         " in den naechsten " + String(days) + " Tagen ab.";
            NtfyNotifier::send(device_config.getNtfyUrl(),
                               device_config.getNtfyTopic(),
                               title, msg, "high");
        }
    }

    // Auto-dismiss result screen after 5 seconds
    if (workflow == WorkflowMode::RESULT
            && _resultShownMs != 0
            && millis() - _resultShownMs >= RESULT_AUTO_DISMISS_MS) {
        _resultShownMs = 0;
        if (_activeTab == UiTab::MANUAL_ENTRY) {
            // Return directly to manual-entry keyboard — skip the intermediate screen
            workflow = WorkflowMode::KB_ENTRY;
            _kbText  = "";
            display_obj.kbReset();
            display_obj.showKeyboardEntry("PRODUKTNAME", "");
        } else if (_activeTab == UiTab::MANUAL_PRODUCT) {
            // Skip intermediate "Produkt auswaehlen" screen — go straight to category picker
            workflow = WorkflowMode::TMPL_CATEGORY;
            showTmplCategories();
        } else {
            workflow = WorkflowMode::HOME;
            renderActiveTab("");
        }
    }

    handleTouch();
    processWorkflow();

    ScanResult scan;
    if (barcode_manager.readScan(scan)) {
        handleScan(scan);
    }

    // Display standby
    if (_standbyMs > 0 && _displayOn && (millis() - _lastActivityMs >= _standbyMs)) {
        setBacklight(false);
    }

    yield();
}

void App::initBacklight() {
    if (LCD_BL < 0) {
        Logger::warn("Display", "Backlight pin disabled");
        return;
    }

    pinMode(LCD_BL, OUTPUT);
    if (ledcAttach(LCD_BL, 5000, 8)) {
        ledcWrite(LCD_BL, 255);
    } else {
        digitalWrite(LCD_BL, HIGH);
        Logger::warn("Display", "PWM attach failed; using digital backlight ON");
    }
    Logger::info("Display", String("Backlight ON GPIO ") + LCD_BL);
}

void App::setBacklight(bool on) {
    _displayOn = on;
    if (LCD_BL < 0) return;
    ledcWrite(LCD_BL, on ? 255 : 0);
}

void App::loadDisplayConfig() {
    JsonDocument doc;
    if (AppFS::fs().exists("/display_config.json")) {
        File f = AppFS::fs().open("/display_config.json", "r");
        if (f) { deserializeJson(doc, f); f.close(); }
    }
    uint32_t secs = doc["standby_sec"] | 0;
    _standbyMs = secs * 1000UL;
    _lastActivityMs = millis();
    Logger::info("Display", String("Standby: ") + (secs ? String(secs) + "s" : "nie"));
}

void App::initI2C() {
    i2c_bus.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);
}

void App::resetLCDViaTCA9554() {
    // TCA9554 at I2C 0x20:
    //   EXIO1 (bit 1) = LCD_RST
    //   EXIO7 (bit 7) = PA_CTRL  → NS4150B speaker amp enable (active-high)
    constexpr uint8_t TCA_ADDR   = 0x20;
    constexpr uint8_t REG_OUTPUT = 0x01;
    constexpr uint8_t REG_CONFIG = 0x03;
    constexpr uint8_t RST_BIT    = 0x02;  // EXIO1
    constexpr uint8_t PA_BIT     = 0x80;  // EXIO7
    constexpr uint8_t OUT_BITS   = RST_BIT | PA_BIT;

    auto tca_write = [&](uint8_t reg, uint8_t val) -> uint8_t {
        i2c_bus.beginTransmission(TCA_ADDR);
        i2c_bus.write(reg);
        i2c_bus.write(val);
        return i2c_bus.endTransmission();
    };
    auto tca_read = [&](uint8_t reg) -> uint8_t {
        i2c_bus.beginTransmission(TCA_ADDR);
        i2c_bus.write(reg);
        i2c_bus.endTransmission(false);
        i2c_bus.requestFrom((uint8_t)TCA_ADDR, (uint8_t)1);
        return i2c_bus.available() ? i2c_bus.read() : 0xFF;
    };

    uint8_t e1 = tca_write(REG_CONFIG, (uint8_t)(~OUT_BITS));   // EXIO1 + EXIO7 = outputs
    uint8_t e2 = tca_write(REG_OUTPUT, 0xFF);         // both high
    delay(10);
    tca_write(REG_OUTPUT, PA_BIT);        // LCD low (assert reset), PA still high
    delay(10);
    tca_write(REG_OUTPUT, 0xFF);          // LCD high (release reset), PA still high
    delay(200);                           // ST7796 needs ~120 ms to wake

    // Readback: verify PA_CTRL (bit 7) is actually HIGH
    uint8_t out_rb  = tca_read(REG_OUTPUT);
    uint8_t cfg_rb  = tca_read(REG_CONFIG);
    bool pa_high    = (out_rb & PA_BIT) != 0;
    Logger::info("Display", String("TCA9554 LCD reset done")
        + "  cfg_err=" + e1 + " out_err=" + e2
        + "  OUT=0x" + String(out_rb, HEX)
        + "  CFG=0x" + String(cfg_rb, HEX)
        + "  PA_CTRL(EXIO7)=" + (pa_high ? "HIGH✓" : "LOW!"));
}

void App::renderDashboard(const String &message) {
    _activeTab = UiTab::STORE;
    renderActiveTab(message, true);
}

// FNV-1a 32-bit hash of a string (fast, collision-resistant enough for UI change detection)
static uint32_t fnv1a(const String &s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < s.length(); i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t App::buildUiHash() const {
    String sig =
        String(static_cast<int>(_activeTab)) + "|" +
        wifi_manager.getSSID() + "|" +
        wifi_manager.getIPAddress() + "|" +
        String(wifi_manager.isConnected()) + "|" +
        ble_scanner.getStatus() + "|" +
        ble_scanner.getDeviceAddress() + "|" +
        ble_scanner.getDeviceName() + "|" +
        barcode_manager.getLastScan() + "|" +
        barcode_manager.getLastType() + "|" +
        String(static_cast<unsigned>(inventory.items().size())) + "|" +
        _statusMessage;
    return fnv1a(sig);
}

void App::renderActiveTab(const String &message, bool force) {
    if (!message.isEmpty()) _statusMessage = message;

    // MANUAL_ENTRY tab never shows the home screen — jump straight to keyboard
    if (_activeTab == UiTab::MANUAL_ENTRY && workflow == WorkflowMode::HOME) {
        workflow = WorkflowMode::KB_ENTRY;
        _kbText  = "";
        display_obj.kbReset();
        display_obj.showKeyboardEntry("PRODUKTNAME", "");
        return;
    }

    // INVENTORY tab always shows the live list, not the empty-placeholder panel
    if (_activeTab == UiTab::INVENTORY && workflow == WorkflowMode::HOME) {
        display_obj.showInventoryList(inventory.items(), _invFilter,
            device_config.getHouseholdAbbr(), _invExpandedGroup, _invScrollOffset);
        _lastUiHash = buildUiHash();
        _lastUiRefreshMs = millis();
        return;
    }

    uint32_t hash = buildUiHash();
    if (!force && hash == _lastUiHash) {
        _lastUiRefreshMs = millis();
        return;
    }

    String wifi_ssid = wifi_manager.getSSID();
    String wifi_ip = wifi_manager.getIPAddress();
    bool wifi_connected = wifi_manager.isConnected();

    String scannerName = ble_scanner.getDeviceName();
    if (scannerName.isEmpty()) scannerName = ble_scanner.getDeviceAddress();
    if (scannerName.isEmpty()) scannerName = "nicht gekoppelt";

    display_obj.showHome(
        _activeTab,
        wifi_ssid,
        wifi_ip,
        wifi_connected,
        ble_scanner.getStatus(),
        scannerName,
        barcode_manager.getLastScan(),
        barcode_manager.getLastType(),
        inventory.items().size(),
        countExpiringSoon(7),
        _statusMessage,
        AppFS::usingSD(),
        labelCounter.getRemaining(),
        ble_scanner.getBatteryLevel());
    _lastUiHash = hash;
    _lastUiRefreshMs = millis();
}

void App::handleTouch() {
    OnscreenAction action = display_obj.hitTest(0, 0);
    if (action == OnscreenAction::NONE) return;

    // Any touch resets the standby timer; if the display was off, just wake it
    _lastActivityMs = millis();
    if (!_displayOn) {
        setBacklight(true);
        return;  // swallow the touch — don't trigger accidental actions
    }

    processOnscreenAction(action);
}

char App::digitForAction(OnscreenAction action) const {
    switch (action) {
        case OnscreenAction::DATE_DIGIT_0: return '0';
        case OnscreenAction::DATE_DIGIT_1: return '1';
        case OnscreenAction::DATE_DIGIT_2: return '2';
        case OnscreenAction::DATE_DIGIT_3: return '3';
        case OnscreenAction::DATE_DIGIT_4: return '4';
        case OnscreenAction::DATE_DIGIT_5: return '5';
        case OnscreenAction::DATE_DIGIT_6: return '6';
        case OnscreenAction::DATE_DIGIT_7: return '7';
        case OnscreenAction::DATE_DIGIT_8: return '8';
        case OnscreenAction::DATE_DIGIT_9: return '9';
        default: return 0;
    }
}

// Converts "DD.MM.YYYY" to a sortable integer YYYYMMDD for date comparison.
// Returns 0 for empty/invalid strings (treated as "no date").
static long dmyToInt(const String &s) {
    if (s.length() < 10) return 0;
    return s.substring(6, 10).toInt() * 10000L
         + s.substring(3, 5).toInt() * 100L
         + s.substring(0, 2).toInt();
}

// Converts "YYYY-MM-DD" (ISO, from TimeManager/web) → "DD.MM.YYYY".
// Passes through anything that is already in DD.MM.YYYY or another format.
static String isoToDMY(const String &iso) {
    if (iso.length() == 10 && iso[4] == '-' && iso[7] == '-')
        return iso.substring(8, 10) + "." + iso.substring(5, 7) + "." + iso.substring(0, 4);
    return iso;
}

// Finds the earliest expiryDate among all items sharing the same product barcode,
// excluding the item with the given labelBarcode. Returns "" if none found.
static String findEarlierExpiry(const std::vector<InventoryItem> &items,
                                const String &productBarcode,
                                const String &excludeLabel,
                                const String &thanDate) {
    if (productBarcode.isEmpty() || thanDate.isEmpty()) return "";
    long limit = dmyToInt(thanDate);
    String earliest = "";
    long earliestInt = LONG_MAX;
    for (const auto &it : items) {
        if (it.labelBarcode == excludeLabel) continue;
        if (it.barcode != productBarcode)   continue;
        if (it.expiryDate.isEmpty())        continue;
        long v = dmyToInt(it.expiryDate);
        if (v > 0 && v < limit && v < earliestInt) {
            earliestInt = v;
            earliest    = it.expiryDate;
        }
    }
    return earliest;
}

static String normalizeSearch(const String &s) {
    String out = s;
    out.toLowerCase();
    out.replace("\xE4", "a");   // ä
    out.replace("\xF6", "o");   // ö
    out.replace("\xFC", "u");   // ü
    out.replace("\xDF", "ss");  // ß
    out.replace("\xC4", "a");   // Ä
    out.replace("\xD6", "o");   // Ö
    out.replace("\xDC", "u");   // Ü
    return out;
}

static bool isSubseq(const String &needle, const String &hay) {
    int ni = 0;
    for (int hi = 0; hi < (int)hay.length() && ni < (int)needle.length(); hi++)
        if (hay[hi] == needle[ni]) ni++;
    return ni == (int)needle.length();
}

String App::bestMatchForSearch(const String &query) const {
    if (query.isEmpty()) return "";
    String q = normalizeSearch(query);
    for (const auto &item : inventory.items())
        if (normalizeSearch(item.name).indexOf(q) >= 0) return item.name;
    for (const auto &item : inventory.items())
        if (isSubseq(q, normalizeSearch(item.name))) return item.name;
    return "";
}

void App::loadManualNames() {
    _manualNames.clear();
    File f = AppFS::fs().open("/manual_names.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        for (JsonVariant v : doc.as<JsonArray>())
            _manualNames.push_back(v.as<String>());
    }
    f.close();
}

void App::saveManualName(const String &name) {
    if (name.isEmpty()) return;
    for (const auto &n : _manualNames)
        if (n.equalsIgnoreCase(name)) return;
    _manualNames.push_back(name);
    while (_manualNames.size() > 200)
        _manualNames.erase(_manualNames.begin());
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &n : _manualNames) arr.add(n);
    File f = AppFS::fs().open("/manual_names.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

String App::bestMatchForName(const String &query) const {
    if (query.isEmpty()) return "";
    String q = normalizeSearch(query);
    for (const auto &n : _manualNames)
        if (normalizeSearch(n).indexOf(q) >= 0) return n;
    for (const auto &n : _manualNames)
        if (isSubseq(q, normalizeSearch(n))) return n;
    return "";
}

String App::bestMatchForSorte(const String &query) const {
    if (query.isEmpty()) return "";
    auto products = templatesForCategory(_selectedCategory);
    if (_selectedTemplateIdx < 0 || _selectedTemplateIdx >= (int)products.size()) return "";
    const auto &sorten = products[_selectedTemplateIdx].sorten;
    String q = normalizeSearch(query);
    for (const auto &s : sorten)
        if (normalizeSearch(s).indexOf(q) >= 0) return s;
    for (const auto &s : sorten)
        if (isSubseq(q, normalizeSearch(s))) return s;
    return "";
}

String App::kbEntryTitle() const {
    if (!_creatingNewSorte) return "PRODUKTNAME";
    auto products = templatesForCategory(_selectedCategory);
    if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size())
        return "NEUE SORTE: " + products[_selectedTemplateIdx].name;
    return "NEUE SORTE";
}

String App::kbEntrySuggestion() const {
    return _creatingNewSorte ? bestMatchForSorte(_kbText) : bestMatchForName(_kbText);
}

void App::processOnscreenAction(OnscreenAction action) {
    char digit = digitForAction(action);
    if (digit && workflow == WorkflowMode::ENTER_DATE) {
        if (_pendingDateDraft.length() < 6) {
            _pendingDateDraft += digit;
            audio_obj.playClickTone();
        }
        if (_pendingDateDraft.length() == 6) {
            // Auto-confirm once all 6 digits are entered
            if (formatDateDraft(_pendingExpiryDate)) {
                _pendingQuantity = 0;  // reset so first tap selects, second confirms
                workflow = WorkflowMode::ENTER_QTY;
                state.setState(AppState::ENTER_QTY);
                display_obj.showQuantityEntry(_pendingProduct, _pendingExpiryDate, _pendingQuantity);
            } else {
                audio_obj.playTone(250, 160);
                display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
            }
        } else {
            display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
        }
        return;
    }
    if (digit && workflow == WorkflowMode::TMPL_AMOUNT) {
        if (_pendingAmountDraft.length() < 5) {
            _pendingAmountDraft += digit;
            audio_obj.playClickTone();
            display_obj.showAmountEntry(_pendingProduct.name, _pendingUnit, _pendingAmountDraft);
        }
        return;
    }
    if (digit && workflow == WorkflowMode::NEW_ROLL_ENTRY) {
        if (_rollDraft.length() < 5) {
            _rollDraft += digit;
            audio_obj.playClickTone();
            display_obj.showNewRollEntry(_rollDraft);
        }
        return;
    }

    // Accept autocomplete suggestion by tapping input bar
    if (action == OnscreenAction::KB_SUGGEST && workflow == WorkflowMode::KB_ENTRY) {
        String sug = kbEntrySuggestion();
        if (!sug.isEmpty()) {
            audio_obj.playClickTone();
            _kbText = sug;
            display_obj.showKeyboardEntry(kbEntryTitle(), _kbText, "");
        }
        return;
    }

    // Keyboard entry: char input (KB_ENTRY, WIFI_SETUP_PASS, INV_SEARCH share the keyboard)
    if (action == OnscreenAction::KB_CHAR &&
            (workflow == WorkflowMode::KB_ENTRY || workflow == WorkflowMode::WIFI_SETUP_PASS
             || workflow == WorkflowMode::INV_SEARCH)) {
        char c = display_obj.drainKbChar();
        if (c != 0) {
            audio_obj.playClickTone();
            _kbText += c;
            if (workflow == WorkflowMode::WIFI_SETUP_PASS) {
                display_obj.showKeyboardEntry("PASSWORT: " + _selectedSsid, _kbText);
            } else if (workflow == WorkflowMode::INV_SEARCH) {
                display_obj.showSearchEntry(_kbText, bestMatchForSearch(_kbText));
            } else {
                display_obj.kbAutoShift(c);
                display_obj.showKeyboardEntry(kbEntryTitle(), _kbText, kbEntrySuggestion());
            }
        }
        return;
    }

    if (action == OnscreenAction::KB_CAPS &&
            (workflow == WorkflowMode::KB_ENTRY || workflow == WorkflowMode::WIFI_SETUP_PASS
             || workflow == WorkflowMode::INV_SEARCH)) {
        audio_obj.playClickTone();
        display_obj.kbToggleCaps();
        if (workflow == WorkflowMode::WIFI_SETUP_PASS)
            display_obj.showKeyboardEntry("PASSWORT: " + _selectedSsid, _kbText);
        else if (workflow == WorkflowMode::INV_SEARCH)
            display_obj.showSearchEntry(_kbText, bestMatchForSearch(_kbText));
        else
            display_obj.showKeyboardEntry(kbEntryTitle(), _kbText, kbEntrySuggestion());
        return;
    }

    if (action == OnscreenAction::KB_BACKSPACE && workflow == WorkflowMode::WIFI_SETUP_PASS) {
        if (!_kbText.isEmpty()) _kbText.remove(_kbText.length() - 1);
        display_obj.showKeyboardEntry("PASSWORT: " + _selectedSsid, _kbText);
        return;
    }

    if (action == OnscreenAction::KB_BACKSPACE && workflow == WorkflowMode::INV_SEARCH) {
        audio_obj.playClickTone();
        if (!_kbText.isEmpty()) _kbText.remove(_kbText.length() - 1);
        display_obj.showSearchEntry(_kbText, bestMatchForSearch(_kbText));
        return;
    }

    // Swipe-right collapses expanded inventory group before navigating
    if (action == OnscreenAction::SWIPE_RIGHT && !_invExpandedGroup.isEmpty()) {
        _invExpandedGroup = "";
        display_obj.showInventoryList(inventory.items(), _invFilter,
                                      device_config.getHouseholdAbbr(), _invExpandedGroup, _invScrollOffset);
        return;
    }

    // Swipe-right = back inside WiFi setup, template workflow, inventory search, or qty entry
    if (action == OnscreenAction::SWIPE_RIGHT) {
        if (workflow == WorkflowMode::ENTER_QTY) {
            audio_obj.playSwipeTone();
            workflow = WorkflowMode::ENTER_DATE;
            state.setState(AppState::ENTER_DATE);
            display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
            return;
        }
        if (workflow == WorkflowMode::INV_SEARCH) {
            audio_obj.playSwipeTone();
            _invFilter = "";
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::INVENTORY;
            display_obj.showInventoryList(inventory.items(), "",
                                          device_config.getHouseholdAbbr(), _invExpandedGroup, _invScrollOffset);
            return;
        }
        if (workflow == WorkflowMode::WIFI_SETUP_PASS) {
            display_obj.showListScreen("WLAN AUSWAEHLEN", _wifiNets);
            workflow = WorkflowMode::WIFI_SETUP_LIST;
            return;
        }
        if (workflow == WorkflowMode::WIFI_SETUP_LIST) {
            WiFi.scanNetworks(/*async=*/true, /*hidden=*/true);
            workflow = WorkflowMode::WIFI_SETUP_SCAN;
            display_obj.showWifiScan();
            return;
        }
        if (workflow == WorkflowMode::TMPL_PRODUCT) {
            audio_obj.playSwipeTone();
            workflow = WorkflowMode::TMPL_CATEGORY;
            showTmplCategories();
            return;
        }
        if (workflow == WorkflowMode::TMPL_BRAND) {
            audio_obj.playSwipeTone();
            workflow = WorkflowMode::TMPL_PRODUCT;
            showTmplProducts();
            return;
        }
        if (workflow == WorkflowMode::TMPL_SORTE) {
            audio_obj.playSwipeTone();
            auto products = templatesForCategory(_selectedCategory);
            if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()
                    && products[_selectedTemplateIdx].brands.size() > 1) {
                workflow = WorkflowMode::TMPL_BRAND;
                showTmplBrands();
            } else {
                workflow = WorkflowMode::TMPL_PRODUCT;
                showTmplProducts();
            }
            return;
        }
        if (workflow == WorkflowMode::TMPL_AMOUNT) {
            audio_obj.playSwipeTone();
            auto products = templatesForCategory(_selectedCategory);
            bool hasSorten = _selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()
                && products[_selectedTemplateIdx].useSorten;
            if (hasSorten) {
                workflow = WorkflowMode::TMPL_SORTE;
                showTmplSorten();
            } else if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()
                    && products[_selectedTemplateIdx].brands.size() > 1) {
                workflow = WorkflowMode::TMPL_BRAND;
                showTmplBrands();
            } else {
                workflow = WorkflowMode::TMPL_PRODUCT;
                showTmplProducts();
            }
            return;
        }
        if (workflow == WorkflowMode::NEW_ROLL_ENTRY
                || workflow == WorkflowMode::NEW_ROLL_CONFIRM) {
            audio_obj.playSwipeTone();
            workflow = WorkflowMode::HOME;
            renderActiveTab("");
            return;
        }
    }

    // Inventory group tap (expand / collapse) — only when actually on inventory list, not location-select overlay
    if (_activeTab == UiTab::INVENTORY && workflow == WorkflowMode::HOME &&
        (action == OnscreenAction::LIST_ITEM_0 || action == OnscreenAction::LIST_ITEM_1 ||
         action == OnscreenAction::LIST_ITEM_2 || action == OnscreenAction::LIST_ITEM_3 ||
         action == OnscreenAction::LIST_ITEM_4)) {
        String tapped = display_obj.getInvGroupName(
            action == OnscreenAction::LIST_ITEM_0 ? 0 :
            action == OnscreenAction::LIST_ITEM_1 ? 1 :
            action == OnscreenAction::LIST_ITEM_2 ? 2 :
            action == OnscreenAction::LIST_ITEM_3 ? 3 : 4);
        if (!tapped.isEmpty()) {
            if (_invExpandedGroup == tapped)
                _invExpandedGroup = "";   // collapse
            else
                _invExpandedGroup = tapped;  // expand
            display_obj.showInventoryList(inventory.items(), _invFilter,
                                          device_config.getHouseholdAbbr(), _invExpandedGroup, _invScrollOffset);
        }
        return;
    }

    // Template list selection
    if (action >= OnscreenAction::LIST_ITEM_0 && action <= OnscreenAction::LIST_ITEM_6) {
        int idx = static_cast<int>(action) - static_cast<int>(OnscreenAction::LIST_ITEM_0);
        if (workflow == WorkflowMode::WIFI_SETUP_LIST) {
            if (idx >= 0 && idx < (int)_wifiNets.size()) {
                _selectedSsid = _wifiNets[idx];
                _kbText = "";
                display_obj.kbReset();
                display_obj.showKeyboardEntry("PASSWORT: " + _selectedSsid, "");
                workflow = WorkflowMode::WIFI_SETUP_PASS;
            }
            return;
        } else if (workflow == WorkflowMode::TMPL_CATEGORY) {
            auto cats = templateCategories();
            if (idx < (int)cats.size()) {
                _selectedCategory   = cats[idx];
                workflow            = WorkflowMode::TMPL_PRODUCT;
                showTmplProducts();
            }
        } else if (workflow == WorkflowMode::TMPL_PRODUCT) {
            _selectedTemplateIdx = idx;
            startTmplMHD();
        } else if (workflow == WorkflowMode::TMPL_BRAND) {
            auto products = templatesForCategory(_selectedCategory);
            if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()) {
                const ProductTemplate &tmpl = products[_selectedTemplateIdx];
                if (idx < (int)tmpl.brands.size()) {
                    _pendingProduct.brand = tmpl.brands[idx];
                    _pendingAmountDraft   = "";
                    _pendingAmount        = 1;
                    _selectedSorte        = "";
                    if (tmpl.useSorten) {
                        workflow = WorkflowMode::TMPL_SORTE;
                        showTmplSorten();
                    } else if (!_pendingUnit.isEmpty()) {
                        workflow = WorkflowMode::TMPL_AMOUNT;
                        display_obj.showAmountEntry(_pendingProduct.name, _pendingUnit, "");
                    } else {
                        _pendingExpiryDate = calcMHD(tmpl.shelfDays, 0);
                        workflow = WorkflowMode::SAVING;
                        state.setState(AppState::SAVING);
                        finishStorageWorkflow();
                    }
                }
            }
        } else if (workflow == WorkflowMode::TMPL_SORTE) {
            auto products = templatesForCategory(_selectedCategory);
            if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()) {
                const ProductTemplate &tmpl = products[_selectedTemplateIdx];
                if (idx == 0) {
                    // "Neu erstellen" — open keyboard
                    _creatingNewSorte = true;
                    _kbText = "";
                    display_obj.kbReset();
                    display_obj.showKeyboardEntry("NEUE SORTE: " + tmpl.name, "");
                    workflow = WorkflowMode::KB_ENTRY;
                } else {
                    int sorteIdx = idx - 1;
                    if (sorteIdx < (int)tmpl.sorten.size()) {
                        _selectedSorte = tmpl.sorten[sorteIdx];
                        proceedFromSorte();
                    }
                }
            }
        } else if (workflow == WorkflowMode::LOCATION_SELECT) {
            auto locs = loadLocationNames();
            // idx 0 = "Kein Lagerort" (clear), then actual locations
            if (idx == 0) {
                device_config.setActiveLocation("");
                device_config.setActiveLocationColor("");
                display_obj.setActiveLocation("");
                display_obj.setActiveLocationColor("");
            } else {
                int locIdx = idx - 1;
                if (locIdx < (int)locs.size()) {
                    const String &locName = locs[locIdx];
                    String color = loadLocationColor(locName);
                    device_config.setActiveLocation(locName);
                    device_config.setActiveLocationColor(color);
                    display_obj.setActiveLocation(locName);
                    display_obj.setActiveLocationColor(color);
                }
            }
            audio_obj.playClickTone();
            workflow = WorkflowMode::HOME;
            renderActiveTab(String("Lagerort: ") + device_config.getActiveLocation());
            return;
        }
        return;
    }

    if (action == OnscreenAction::FIFO_CONFIRM && workflow == WorkflowMode::FIFO_WARN) {
        bool removed = inventory.removeByLabel(_fifoLabelCode);
        if (removed) {
            audio_obj.playCheckoutTone();
            JsonDocument sdoc;
            sdoc["type"]         = "REMOVE_LABEL";
            sdoc["labelBarcode"] = _fifoLabelCode;
            sdoc["household"]    = device_config.getHousehold();
            sdoc["deviceName"]   = device_config.getDeviceName();
            sdoc["timestamp"]    = (long)time(nullptr);
            String sp; serializeJson(sdoc, sp);
            sync_manager.enqueue("REMOVE_LABEL", sp);
        }
        workflow = WorkflowMode::RESULT;
        _resultShownMs = millis();
        _resultSuccess = removed;
        _resultTitle   = removed ? "Ausgelagert" : "Nicht gefunden";
        _resultMessage = removed
            ? (_fifoProductName.isEmpty() ? _fifoLabelCode : _fifoProductName)
            : "Label ist nicht im Inventar";
        display_obj.showResult(_resultTitle, _resultMessage, _resultSuccess);
        return;
    }

    if (action == OnscreenAction::FIFO_CANCEL && workflow == WorkflowMode::FIFO_WARN) {
        workflow = WorkflowMode::RESULT;
        _resultShownMs = millis();
        _resultSuccess = true;
        _resultTitle   = "Abgebrochen";
        _resultMessage = "Auslagerung abgebrochen";
        display_obj.showResult(_resultTitle, _resultMessage, true);
        return;
    }

    // ── SWIPE_LEFT in TMPL_SORTE: delete sorte by detecting swiped row ──────
    if (action == OnscreenAction::SWIPE_LEFT && workflow == WorkflowMode::TMPL_SORTE) {
        int pressY = display_obj.getLastSwipePressY();
        int rowIdx = (pressY - HDR_H) / 55; // ITEM_H = 55px in showListScreen
        auto products = templatesForCategory(_selectedCategory);
        if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()) {
            const ProductTemplate &tmpl = products[_selectedTemplateIdx];
            int sorteIdx = rowIdx - 1; // row 0 = "Neue Sorte erstellen"
            if (sorteIdx >= 0 && sorteIdx < (int)tmpl.sorten.size()) {
                _pendingSorteDeleteIdx = sorteIdx;
                display_obj.showConfirmDialog("Sorte loeschen?",
                    "\"" + tmpl.sorten[sorteIdx] + "\" entfernen?");
                return;
            }
        }
        return; // swipe on non-deletable row → ignore
    }

    // ── SWIPE_UP/DOWN: inventory list scroll ──────────────────────────────
    if (action == OnscreenAction::SWIPE_UP && _activeTab == UiTab::INVENTORY && workflow == WorkflowMode::HOME) {
        _invScrollOffset++;
        display_obj.showInventoryList(inventory.items(), _invFilter,
                                      device_config.getHouseholdAbbr(), _invExpandedGroup, _invScrollOffset);
        return;
    }
    if (action == OnscreenAction::SWIPE_DOWN && _activeTab == UiTab::INVENTORY && workflow == WorkflowMode::HOME) {
        if (_invScrollOffset > 0) _invScrollOffset--;
        display_obj.showInventoryList(inventory.items(), _invFilter,
                                      device_config.getHouseholdAbbr(), _invExpandedGroup, _invScrollOffset);
        return;
    }

    switch (action) {
        // ── Tab navigation ──────────────────────────────────────────────────
        case OnscreenAction::SWIPE_LEFT: {
            _kbConfirmPending = false;
            _pendingSorteDeleteIdx = -1;
            audio_obj.playSwipeTone();
            static const UiTab CYCLE[] = {
                UiTab::STORE, UiTab::INVENTORY, UiTab::SYSTEM,
                UiTab::MANUAL_PRODUCT, UiTab::MANUAL_ENTRY
            };
            static constexpr int NC = 5;
            int ci = 0;
            for (int i = 0; i < NC; i++) if (CYCLE[i] == _activeTab) { ci = i; break; }
            action = static_cast<OnscreenAction>(
                static_cast<int>(OnscreenAction::TAB_STORE) +
                static_cast<int>(CYCLE[(ci + 1) % NC]));
            processOnscreenAction(action);
            return;
        }
        case OnscreenAction::SWIPE_RIGHT: {
            audio_obj.playSwipeTone();
            static const UiTab CYCLE[] = {
                UiTab::STORE, UiTab::INVENTORY, UiTab::SYSTEM,
                UiTab::MANUAL_PRODUCT, UiTab::MANUAL_ENTRY
            };
            static constexpr int NC = 5;
            int ci = 0;
            for (int i = 0; i < NC; i++) if (CYCLE[i] == _activeTab) { ci = i; break; }
            action = static_cast<OnscreenAction>(
                static_cast<int>(OnscreenAction::TAB_STORE) +
                static_cast<int>(CYCLE[(ci + NC - 1) % NC]));
            processOnscreenAction(action);
            return;
        }
        case OnscreenAction::SWIPE_DOWN:
            workflow = WorkflowMode::LOCATION_SELECT;
            showLocationSelect();
            return;
        case OnscreenAction::TAB_STORE:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::STORE;
            renderActiveTab("EAN scannen zum Einlagern");
            break;
        case OnscreenAction::TAB_INVENTORY:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::INVENTORY;
            _invFilter = "";
            _invExpandedGroup = "";
            _invScrollOffset = 0;
            display_obj.showInventoryList(inventory.items(), "",
                                          device_config.getHouseholdAbbr(), _invExpandedGroup, 0);
            _lastUiRefreshMs = millis();
            break;

        case OnscreenAction::INV_SEARCH:
            if (workflow == WorkflowMode::INV_SEARCH) {
                // Already in search: tap on input bar = accept suggestion
                String suggestion = bestMatchForSearch(_kbText);
                if (!suggestion.isEmpty()) {
                    audio_obj.playClickTone();
                    _kbText = suggestion;
                    display_obj.showSearchEntry(_kbText, "");
                }
            } else {
                workflow = WorkflowMode::INV_SEARCH;
                _activeTab = UiTab::INVENTORY;
                _kbText = _invFilter;
                display_obj.kbReset();
                display_obj.showSearchEntry(_kbText, bestMatchForSearch(_kbText));
            }
            break;
        case OnscreenAction::TAB_SCANNER:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::SYSTEM;
            renderActiveTab("Systemstatus und Setup AP");
            break;
        case OnscreenAction::TAB_SYSTEM:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::SYSTEM;
            renderActiveTab("Systemstatus und Setup AP");
            break;
        case OnscreenAction::TAB_MANUAL_PRODUCT:
            workflow = WorkflowMode::TMPL_CATEGORY;
            _activeTab = UiTab::MANUAL_PRODUCT;
            loadTemplates();  // reload in case Web UI added templates
            showTmplCategories();
            break;
        case OnscreenAction::TAB_MANUAL_ENTRY:
            workflow = WorkflowMode::KB_ENTRY;
            _activeTab = UiTab::MANUAL_ENTRY;
            _kbText = "";
            display_obj.kbReset();
            display_obj.showKeyboardEntry("MANUELL", "");
            break;
        case OnscreenAction::SD_IMPORT_YES:
            if (workflow == WorkflowMode::SD_IMPORT_PROMPT) {
                BackupManager::doRestore();
                // Reload in-memory state from newly restored files
                inventory.begin();
                loadTemplates();
                device_config.begin();
                display_obj.setActiveLocation(device_config.getActiveLocation());
                display_obj.setActiveLocationColor(device_config.getActiveLocationColor());
                workflow = WorkflowMode::HOME;
                Logger::info("Backup", "Import abgeschlossen");
            }
            break;
        case OnscreenAction::SD_IMPORT_NO:
            if (workflow == WorkflowMode::SD_IMPORT_PROMPT)
                workflow = WorkflowMode::HOME;
            break;

        case OnscreenAction::REFRESH:
            if (workflow == WorkflowMode::RESULT && _activeTab == UiTab::MANUAL_PRODUCT) {
                workflow = WorkflowMode::TMPL_CATEGORY;
                showTmplCategories();
            } else {
                workflow = WorkflowMode::HOME;
                renderActiveTab("Anzeige aktualisiert");
            }
            break;
        case OnscreenAction::WIFI_SETUP:
            WiFi.scanNetworks(/*async=*/true, /*hidden=*/true);
            workflow = WorkflowMode::WIFI_SETUP_SCAN;
            display_obj.showWifiScan();
            break;
        case OnscreenAction::SCANNER_RECONNECT:
            if (ble_scanner.isConnected() || ble_scanner.isConnecting()) {
                ble_scanner.disconnect();
                renderActiveTab("Scanner getrennt");
            } else if (!ble_scanner.getDeviceAddress().isEmpty()) {
                ble_scanner.requestConnect(ble_scanner.getDeviceAddress(), ble_scanner.getDeviceName());
                renderActiveTab("Scanner-Verbindung wird aufgebaut");
            } else {
                renderActiveTab("Scanner im Web-UI koppeln");
            }
            break;
        case OnscreenAction::DATE_BACKSPACE:
            if (workflow == WorkflowMode::ENTER_DATE && !_pendingDateDraft.isEmpty()) {
                _pendingDateDraft.remove(_pendingDateDraft.length() - 1);
                display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
            } else if (workflow == WorkflowMode::TMPL_AMOUNT && !_pendingAmountDraft.isEmpty()) {
                _pendingAmountDraft.remove(_pendingAmountDraft.length() - 1);
                display_obj.showAmountEntry(_pendingProduct.name, _pendingUnit, _pendingAmountDraft);
            } else if (workflow == WorkflowMode::NEW_ROLL_ENTRY && !_rollDraft.isEmpty()) {
                _rollDraft.remove(_rollDraft.length() - 1);
                display_obj.showNewRollEntry(_rollDraft);
            }
            break;
        case OnscreenAction::DATE_CONFIRM:
            if (workflow == WorkflowMode::ENTER_DATE && formatDateDraft(_pendingExpiryDate)) {
                _pendingQuantity = 0;  // reset so first tap selects, second confirms
                workflow = WorkflowMode::ENTER_QTY;
                state.setState(AppState::ENTER_QTY);
                display_obj.showQuantityEntry(_pendingProduct, _pendingExpiryDate, _pendingQuantity);
            } else if (workflow == WorkflowMode::ENTER_DATE) {
                display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
                audio_obj.playTone(250, 160);
            } else if (workflow == WorkflowMode::TMPL_AMOUNT) {
                if (_pendingAmountDraft.isEmpty()) {
                    audio_obj.playTone(250, 160);  // nothing entered yet
                } else {
                    _pendingAmount = _pendingAmountDraft.toInt();
                    _mhdOffset = 0;
                    auto products = templatesForCategory(_selectedCategory);
                    if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()) {
                        _pendingExpiryDate = calcMHD(products[_selectedTemplateIdx].shelfDays, 0);
                        workflow = WorkflowMode::SAVING;
                        state.setState(AppState::SAVING);
                        finishStorageWorkflow();
                    }
                }
            } else if (workflow == WorkflowMode::NEW_ROLL_ENTRY) {
                uint32_t size = _rollDraft.toInt();
                if (size == 0) {
                    audio_obj.playWarningTone();
                } else {
                    labelCounter.newRoll(size);
                    workflow = WorkflowMode::HOME;
                    renderActiveTab("Neue Rolle: " + String(size) + " Labels");
                }
            }
            break;
        // ── Quantity direct selection (buttons 1–12) ───────────────────────
        case OnscreenAction::QTY_1:  case OnscreenAction::QTY_2:
        case OnscreenAction::QTY_3:  case OnscreenAction::QTY_4:
        case OnscreenAction::QTY_5:  case OnscreenAction::QTY_6:
        case OnscreenAction::QTY_7:  case OnscreenAction::QTY_8:
        case OnscreenAction::QTY_9:  case OnscreenAction::QTY_10:
        case OnscreenAction::QTY_11: case OnscreenAction::QTY_12: {
            int qty = static_cast<int>(action) - static_cast<int>(OnscreenAction::QTY_1) + 1;
            audio_obj.playClickTone();
            if (workflow == WorkflowMode::ENTER_QTY) {
                if (qty == _pendingQuantity) {
                    // Second tap on already-selected number: save
                    workflow = WorkflowMode::SAVING;
                    state.setState(AppState::SAVING);
                    finishStorageWorkflow();
                } else {
                    // First tap: highlight selection
                    _pendingQuantity = qty;
                    display_obj.showQuantityEntry(_pendingProduct, _pendingExpiryDate, _pendingQuantity);
                }
            } else { (void)qty; }
            break;
        }
        case OnscreenAction::QTY_MINUS: break;  // no longer drawn; kept for safety
        case OnscreenAction::QTY_PLUS:  break;
        case OnscreenAction::QTY_CONFIRM:
            if (workflow == WorkflowMode::ENTER_QTY) {
                workflow = WorkflowMode::SAVING;
                state.setState(AppState::SAVING);
                finishStorageWorkflow();
            }
            break;
        case OnscreenAction::CANCEL:
            if (workflow == WorkflowMode::TMPL_PRODUCT) {
                workflow = WorkflowMode::TMPL_CATEGORY;
                showTmplCategories();
            } else if (workflow == WorkflowMode::TMPL_BRAND) {
                workflow = WorkflowMode::TMPL_PRODUCT;
                showTmplProducts();
            } else if (workflow == WorkflowMode::TMPL_SORTE) {
                auto products = templatesForCategory(_selectedCategory);
                if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()
                        && products[_selectedTemplateIdx].brands.size() > 1) {
                    workflow = WorkflowMode::TMPL_BRAND;
                    showTmplBrands();
                } else {
                    workflow = WorkflowMode::TMPL_PRODUCT;
                    showTmplProducts();
                }
            } else if (workflow == WorkflowMode::KB_ENTRY && _creatingNewSorte) {
                _creatingNewSorte = false;
                workflow = WorkflowMode::TMPL_SORTE;
                showTmplSorten();
            } else if (workflow == WorkflowMode::TMPL_AMOUNT) {
                auto products = templatesForCategory(_selectedCategory);
                bool hasSorten = _selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()
                    && products[_selectedTemplateIdx].useSorten;
                if (hasSorten) {
                    workflow = WorkflowMode::TMPL_SORTE;
                    showTmplSorten();
                } else if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()
                        && products[_selectedTemplateIdx].brands.size() > 1) {
                    workflow = WorkflowMode::TMPL_BRAND;
                    showTmplBrands();
                } else {
                    workflow = WorkflowMode::TMPL_PRODUCT;
                    showTmplProducts();
                }
            } else if (workflow == WorkflowMode::NEW_ROLL_ENTRY
                    || workflow == WorkflowMode::NEW_ROLL_CONFIRM) {
                workflow = WorkflowMode::HOME;
                renderActiveTab("");
            } else {
                workflow = WorkflowMode::HOME;
                state.setState(AppState::MAIN);
                renderDashboard("Vorgang abgebrochen");
            }
            break;
        case OnscreenAction::PRINTER_FEED_1:
            printer.feed(1);
            break;
        case OnscreenAction::PRINTER_FEED_5:
            printer.feed(5);
            break;
        case OnscreenAction::LOCATION_BADGE:
            audio_obj.playClickTone();
            workflow = WorkflowMode::LOCATION_SELECT;
            showLocationSelect();
            break;

        case OnscreenAction::NEW_ROLL: {
            uint32_t prev = labelCounter.getRollSize();
            if (prev > 0) {
                _prevRollSize = prev;
                workflow = WorkflowMode::NEW_ROLL_CONFIRM;
                display_obj.showConfirmDialog("NEUE ROLLE",
                    "Gleiche Rollengroesse (" + String(prev) + " Labels) wie zuvor?");
            } else {
                _rollDraft = "";
                workflow = WorkflowMode::NEW_ROLL_ENTRY;
                display_obj.showNewRollEntry("");
            }
            break;
        }

        case OnscreenAction::MHD_DAY_MINUS: break;
        case OnscreenAction::MHD_DAY_PLUS:  break;
        case OnscreenAction::MHD_CONFIRM:   break;

        // ── Keyboard entry confirm / backspace ─────────────────────────────
        case OnscreenAction::CONFIRM_YES:
            if (_kbConfirmPending) {
                _kbConfirmPending = false;
                saveManualName(_kbText);
                _pendingProduct.name    = _kbText;
                _pendingProduct.brand   = "";
                _pendingProduct.barcode = "";
                _pendingDateDraft       = "";
                _pendingExpiryDate      = "";
                _pendingQuantity        = 1;
                _pendingUnit            = "";
                workflow = WorkflowMode::ENTER_DATE;
                state.setState(AppState::ENTER_DATE);
                display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
            } else if (_pendingSorteDeleteIdx >= 0 && workflow == WorkflowMode::TMPL_SORTE) {
                auto products = templatesForCategory(_selectedCategory);
                if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()) {
                    removeSorteFromTemplate(products[_selectedTemplateIdx].id, _pendingSorteDeleteIdx);
                }
                _pendingSorteDeleteIdx = -1;
                showTmplSorten();
            } else if (workflow == WorkflowMode::WIFI_SETUP_CONFIRM) {
                wifi_manager.saveCredentials(_selectedSsid.c_str(), _kbText.c_str());
                wifi_manager.connectToWiFi(_selectedSsid.c_str(), _kbText.c_str());
                _wifiConnectStartMs = millis();
                workflow = WorkflowMode::WIFI_SETUP_CONN;
                display_obj.showResult("Verbinde…", _selectedSsid, false);
            } else if (workflow == WorkflowMode::NEW_ROLL_CONFIRM) {
                labelCounter.newRoll(_prevRollSize);
                workflow = WorkflowMode::HOME;
                renderActiveTab("Neue Rolle: " + String(_prevRollSize) + " Labels");
            }
            break;
        case OnscreenAction::CONFIRM_NO:
            if (_kbConfirmPending) {
                _kbConfirmPending = false;
                // Return to keyboard with text still intact
                display_obj.showKeyboardEntry(kbEntryTitle(), _kbText, kbEntrySuggestion());
            } else if (_pendingSorteDeleteIdx >= 0 && workflow == WorkflowMode::TMPL_SORTE) {
                _pendingSorteDeleteIdx = -1;
                showTmplSorten();
            } else if (workflow == WorkflowMode::WIFI_SETUP_CONFIRM) {
                workflow = WorkflowMode::WIFI_SETUP_PASS;
                display_obj.showKeyboardEntry("PASSWORT: " + _selectedSsid, _kbText);
            } else if (workflow == WorkflowMode::NEW_ROLL_CONFIRM) {
                _rollDraft = "";
                workflow = WorkflowMode::NEW_ROLL_ENTRY;
                display_obj.showNewRollEntry("");
            }
            break;
        case OnscreenAction::KB_CONFIRM:
            if (workflow == WorkflowMode::WIFI_SETUP_PASS) {
                workflow = WorkflowMode::WIFI_SETUP_CONFIRM;
                display_obj.showConfirmDialog("WLAN verbinden",
                    "Mit \"" + _selectedSsid + "\" verbinden?");
                break;
            }
            if (workflow == WorkflowMode::INV_SEARCH) {
                _invFilter = _kbText;
                workflow = WorkflowMode::HOME;
                _activeTab = UiTab::INVENTORY;
                {
                    String q = normalizeSearch(_invFilter);
                    std::vector<InventoryItem> filtered;
                    if (_invFilter.isEmpty()) {
                        filtered = inventory.items();
                    } else {
                        for (const auto &it : inventory.items()) {
                            String nl = normalizeSearch(it.name);
                            String ll = normalizeSearch(it.location);
                            if (nl.indexOf(q) >= 0 || ll.indexOf(q) >= 0 ||
                                isSubseq(q, nl) || isSubseq(q, ll))
                                filtered.push_back(it);
                        }
                    }
                    _invExpandedGroup = "";
                    _invScrollOffset = 0;
                    display_obj.showInventoryList(filtered, _invFilter,
                                                  device_config.getHouseholdAbbr(), _invExpandedGroup, 0);
                }
                break;
            }
            if (workflow == WorkflowMode::KB_ENTRY) {
                if (_kbText.isEmpty()) {
                    audio_obj.playWarningTone();
                    break;
                }
                if (_creatingNewSorte) {
                    _selectedSorte    = _kbText;
                    _creatingNewSorte = false;
                    // Persist new sorte in the template JSON
                    auto products = templatesForCategory(_selectedCategory);
                    if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()) {
                        addSorteToTemplate(products[_selectedTemplateIdx].id, _selectedSorte);
                    }
                    proceedFromSorte();
                    break;
                }
                // Show confirmation dialog before proceeding
                _kbConfirmPending = true;
                display_obj.showConfirmDialog("Produktname bestaetigen?", _kbText);
            }
            break;
        case OnscreenAction::KB_BACKSPACE:
            if (workflow == WorkflowMode::KB_ENTRY && !_kbText.isEmpty()) {
                audio_obj.playClickTone();
                _kbText.remove(_kbText.length() - 1);
                if (_kbText.isEmpty()) display_obj.kbAutoShift(' '); // treat empty as after-space
                display_obj.showKeyboardEntry(kbEntryTitle(), _kbText, kbEntrySuggestion());
            }
            break;

        default:
            break;
    }
}

void App::handleScan(const ScanResult &scan) {
    _lastActivityMs = millis();
    if (!_displayOn) setBacklight(true);

    Logger::info("Scanner", String("Barcode: ") + scan.code);

    if (workflow != WorkflowMode::HOME && workflow != WorkflowMode::RESULT && workflow != WorkflowMode::FIFO_WARN) {
        Logger::warn("Scanner", "Ignoring scan while product workflow is active");
        audio_obj.playTone(250, 120);
        return;
    }

    if (scan.type == BarcodeType::LABEL) {
        state.setState(AppState::RETRIEVE);

        // Re-scan of a recently removed label → restore to inventory at current location
        if (!inventory.hasLabel(scan.code)) {
            const InventoryItem *recent = inventory.findRecentByLabel(scan.code);
            if (recent) {
                InventoryItem restored  = *recent;
                restored.location       = device_config.getActiveLocation();
                bool ok = inventory.restoreByLabel(scan.code, restored);
                audio_obj.playSuccessTone();
                workflow       = WorkflowMode::RESULT;
                _resultShownMs = millis();
                _resultSuccess = ok;
                _resultTitle   = ok ? "Eingelagert" : "Fehler";
                _resultMessage = restored.name.isEmpty() ? scan.code : restored.name;
                display_obj.showResult(_resultTitle, _resultMessage, _resultSuccess);
                return;
            }
        }

        // Find item details BEFORE deciding to remove
        String removedName, removedBarcode, removedExpiry;
        for (const auto &it : inventory.items()) {
            if (it.labelBarcode == scan.code) {
                removedName    = it.name;
                removedBarcode = it.barcode;
                removedExpiry  = it.expiryDate;
                break;
            }
        }

        // FIFO check BEFORE removal: warn if older same-product item still in storage
        String olderExpiry = findEarlierExpiry(inventory.items(), removedBarcode, scan.code, removedExpiry);
        if (!olderExpiry.isEmpty()) {
            _fifoLabelCode    = scan.code;
            _fifoProductName  = removedName;
            _fifoEanBarcode   = removedBarcode;
            _fifoOlderExpiry  = olderExpiry;
            _fifoRemovedExpiry = removedExpiry;
            audio_obj.playWarningTone();
            display_obj.showFifoWarning(removedName, olderExpiry);
            workflow = WorkflowMode::FIFO_WARN;
            return;
        }

        // No FIFO warning — proceed with removal
        bool removed = inventory.removeByLabel(scan.code);
        if (removed) {
            audio_obj.playCheckoutTone();
            JsonDocument sdoc;
            sdoc["type"]         = "REMOVE_LABEL";
            sdoc["labelBarcode"] = scan.code;
            sdoc["household"]    = device_config.getHousehold();
            sdoc["deviceName"]   = device_config.getDeviceName();
            sdoc["timestamp"]    = (long)time(nullptr);
            String sp; serializeJson(sdoc, sp);
            sync_manager.enqueue("REMOVE_LABEL", sp);
        } else {
            audio_obj.playErrorTone();
        }
        workflow = WorkflowMode::RESULT; _resultShownMs = millis();
        _resultSuccess = removed;
        _resultTitle   = removed ? "Ausgelagert" : "Nicht gefunden";
        _resultMessage = removed
            ? (removedName.isEmpty() ? scan.code : removedName)
            : "Label ist nicht im Inventar";
        display_obj.showResult(_resultTitle, _resultMessage, _resultSuccess);
        return;
    }

    // EAN scans always go through the full workflow (product lookup → MHD → qty).
    // LebNumber/label barcodes are handled above for removal only.
    audio_obj.playSuccessTone();
    startProductLookup(scan.code);
}

void App::startProductLookup(const String &barcode) {
    _pendingBarcode = barcode;
    _pendingProduct = ProductInfo();
    _pendingDateDraft = "";
    _pendingExpiryDate = "";
    _pendingQuantity = 1;
    _pendingUnit = "";
    _pendingAmount = 1;
    _pendingAmountDraft = "";
    workflow = WorkflowMode::FETCHING_PRODUCT;
    state.setState(AppState::FETCHING);
    display_obj.showFetchingProduct(barcode);
}

void App::fetchTaskFn(void *param) {
    App *self = static_cast<App *>(param);
    self->_fetchOk  = self->openFoodFacts.fetchProduct(self->_pendingBarcode, self->_fetchedProduct);
    self->_fetchTaskHandle = nullptr; // clear before signalling done to avoid stale handle
    self->_fetchDone = true;
    vTaskDelete(nullptr);
}

void App::processWorkflow() {
    if (workflow != WorkflowMode::FETCHING_PRODUCT) return;

    if (!_fetchStarted) {
        // Launch fetch on core 0 (network core) – UI loop continues on core 1
        _fetchStarted    = true;
        _fetchDone       = false;
        _fetchOk         = false;
        _fetchTaskHandle = nullptr;
        _fetchedProduct  = ProductInfo();
        _fetchStartedMs  = millis();
        xTaskCreatePinnedToCore(fetchTaskFn, "api_fetch", 12288, this, 2, &_fetchTaskHandle, 0);
        return;
    }

    if (!_fetchDone) {
        // Abort if the fetch task has been running too long (network stall / server down)
        if (millis() - _fetchStartedMs >= FETCH_TIMEOUT_MS) {
            Logger::warn("Fetch", "Product fetch timed out – aborting task");
            if (_fetchTaskHandle) {
                vTaskDelete(_fetchTaskHandle);
                _fetchTaskHandle = nullptr;
            }
            _fetchDone = true;
            _fetchOk   = false;
        } else {
            return; // Still fetching – keep rendering
        }
    }

    // Fetch complete
    _fetchStarted = false;
    ProductInfo product = _fetchedProduct;
    if (!_fetchOk) {
        product.barcode = _pendingBarcode;
        product.name = "Unbekanntes Produkt";
        product.brand = "Manuell pruefen";
    }

    _pendingProduct = product;
    workflow = WorkflowMode::ENTER_DATE;
    state.setState(AppState::ENTER_DATE);
    display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
    _lastUiRefreshMs = millis();
}

bool App::formatDateDraft(String &formatted) const {
    if (_pendingDateDraft.length() != 6) return false;
    int day   = _pendingDateDraft.substring(0, 2).toInt();
    int month = _pendingDateDraft.substring(2, 4).toInt();
    int year  = 2000 + _pendingDateDraft.substring(4, 6).toInt();
    if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2000 || year > 2099) return false;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", day, month, year);
    formatted = buf;
    return true;
}

bool App::finishStorageWorkflow() {
    state.setState(AppState::PRINTING);

    const int qty  = (_pendingQuantity > 0) ? _pendingQuantity : 1;
    const String name = _pendingProduct.name.isEmpty() ? "Lebensmittel" : _pendingProduct.name;
    int storedCount = 0;

    // Template workflow: always 1 item with the entered food amount.
    // Scan workflow: loop qty times, each item has quantity=1.
    const bool isTemplate = !_pendingUnit.isEmpty();
    const int  loopCount  = isTemplate ? 1 : qty;

    for (int i = 0; i < loopCount; i++) {
        InventoryItem item;
        item.barcode      = _pendingProduct.barcode;
        item.name         = name;
        item.brand        = _pendingProduct.brand;
        item.category     = isTemplate
                                ? (_selectedCategory.isEmpty() ? "Vorlage" : _selectedCategory)
                                : "Barcode";
        item.expiryDate   = _pendingExpiryDate;
        item.addedDate    = isoToDMY(time_manager.today());
        item.quantity     = isTemplate ? _pendingAmount : 1;
        item.unit         = _pendingUnit;
        item.labelBarcode = labelCounter.nextLabel();
        item.location     = device_config.getActiveLocation();

        if (inventory.addItem(item)) {
            JsonDocument sdoc;
            sdoc["type"]         = "ADD";
            sdoc["barcode"]      = item.barcode;
            sdoc["name"]         = item.name;
            sdoc["brand"]        = item.brand;
            sdoc["category"]     = item.category;
            sdoc["expiryDate"]   = item.expiryDate;
            sdoc["addedDate"]    = item.addedDate;
            sdoc["quantity"]     = item.quantity;
            sdoc["unit"]         = item.unit;
            sdoc["labelBarcode"] = item.labelBarcode;
            sdoc["household"]    = device_config.getHousehold();
            sdoc["deviceName"]   = device_config.getDeviceName();
            sdoc["location"]     = item.location;
            sdoc["timestamp"]    = (long)time(nullptr);
            String sp; serializeJson(sdoc, sp);
            sync_manager.enqueue("ADD", sp);
            printer.printLabel(item);
            storedCount++;
        }
    }

    bool stored = storedCount > 0;
    workflow = WorkflowMode::RESULT;
    _resultShownMs = millis();
    state.setState(stored ? AppState::SUCCESS : AppState::ERROR);
    _resultSuccess = stored;
    _resultTitle   = stored ? "Eingelagert" : "Speicherfehler";
    if (stored) {
        if (isTemplate && !_pendingUnit.isEmpty()) {
            _resultMessage = name + " (" + String(_pendingAmount) + " " + _pendingUnit + ") eingelagert";
        } else {
            _resultMessage = storedCount == 1
                ? name + " eingelagert"
                : String(storedCount) + "x " + name + " eingelagert";
        }
    } else {
        _resultMessage = "Inventar konnte nicht gespeichert werden";
    }
    display_obj.showResult(_resultTitle, _resultMessage, stored, false);
    if (stored) audio_obj.playSuccessTone(); else audio_obj.playErrorTone();
    return stored;
}

void App::initFilesystem() {
    Logger::info("LittleFS", "Mounting filesystem");
    if (!fs.begin()) {
        Logger::error("LittleFS", "Mount failed – web files unavailable");
    } else {
        Logger::info("LittleFS", "Mounted OK");
        fs.ensureJsonFile("/inventory.json", "[]");
        fs.ensureJsonFile("/off_cache.json", "[]");
        fs.ensureJsonFile("/locations.json", "[]");
    }
    loadTemplates();
}

// ── Template helpers ──────────────────────────────────────────────────────────

void App::loadTemplates() {
    _templates.clear();
    // Read from the same file as the web Produktvorlagen page (/api/custom-products)
    File f = AppFS::fs().open("/custom_products.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    int idx = 0;
    for (JsonObject obj : doc.as<JsonArray>()) {
        ProductTemplate t;
        t.id        = obj["id"]   | String(idx);
        t.name      = obj["name"] | "";
        t.category  = obj["category"] | "Allgemein";
        // custom-products uses "defaultDays"; map to shelfDays
        t.shelfDays  = obj["defaultDays"] | (obj["shelfDays"] | 14);
        t.unit       = obj["unit"] | "";
        t.useSorten  = obj["useSorten"] | false;
        t.sorten.clear();
        if (obj["sorten"].is<JsonArray>()) {
            for (JsonVariant s : obj["sorten"].as<JsonArray>())
                t.sorten.push_back(s.as<String>());
        }
        t.brands.clear();
        if (obj["brands"].is<JsonArray>()) {
            for (JsonVariant b : obj["brands"].as<JsonArray>())
                t.brands.push_back(b.as<String>());
        } else if (obj["brand"].is<const char *>()) {
            String s = obj["brand"] | "";
            if (!s.isEmpty()) t.brands.push_back(s);
        }
        if (t.shelfDays <= 0) {
            Logger::warn("Templates", String("Vorlage '") + t.name + "' hat ungültige shelfDays=" + t.shelfDays + " – übersprungen");
            continue;
        }
        if (!t.name.isEmpty()) { _templates.push_back(t); idx++; }
    }
    Logger::debug("Templates", String(_templates.size()) + " Vorlagen geladen");
}

std::vector<String> App::templateCategories() const {
    std::vector<String> cats;
    for (const auto &t : _templates) {
        bool found = false;
        for (const auto &c : cats) { if (c == t.category) { found = true; break; } }
        if (!found) cats.push_back(t.category);
    }
    return cats;
}

std::vector<ProductTemplate> App::templatesForCategory(const String &cat) const {
    std::vector<ProductTemplate> out;
    for (const auto &t : _templates) {
        if (t.category == cat) out.push_back(t);
    }
    return out;
}

String App::calcMHD(int shelfDays, int offset) const {
    return isoToDMY(time_manager.addDays(shelfDays + offset));
}

void App::showTmplCategories() {
    auto cats = templateCategories();
    std::vector<String> items;
    for (const auto &c : cats) items.push_back(c);
    display_obj.showCategoryTiles(items);
}

void App::showTmplProducts() {
    auto products = templatesForCategory(_selectedCategory);
    std::vector<String> items;
    for (const auto &p : products)
        items.push_back(p.name + " (" + String(p.shelfDays) + " Tage)");
    display_obj.showListScreen(_selectedCategory.c_str(), items);
}

void App::showTmplBrands() {
    auto products = templatesForCategory(_selectedCategory);
    if (_selectedTemplateIdx < 0 || _selectedTemplateIdx >= (int)products.size()) return;
    const ProductTemplate &tmpl = products[_selectedTemplateIdx];
    display_obj.showListScreen(tmpl.name.c_str(), tmpl.brands);
}

void App::showTmplSorten() {
    auto products = templatesForCategory(_selectedCategory);
    if (_selectedTemplateIdx < 0 || _selectedTemplateIdx >= (int)products.size()) return;
    const ProductTemplate &tmpl = products[_selectedTemplateIdx];
    std::vector<String> items;
    items.push_back("\x1B Neue Sorte erstellen...");  // ESC char as visual marker
    for (const auto &s : tmpl.sorten) items.push_back(s);
    display_obj.showListScreen(("SORTE: " + tmpl.name).c_str(), items);
}

void App::proceedFromSorte() {
    auto products = templatesForCategory(_selectedCategory);
    if (_selectedTemplateIdx < 0 || _selectedTemplateIdx >= (int)products.size()) return;
    const ProductTemplate &tmpl = products[_selectedTemplateIdx];
    // Append sorte to product name
    _pendingProduct.name = _selectedSorte.isEmpty()
        ? tmpl.name
        : tmpl.name + " - " + _selectedSorte;
    _pendingAmountDraft = "";
    _pendingAmount      = 1;
    if (!_pendingUnit.isEmpty()) {
        workflow = WorkflowMode::TMPL_AMOUNT;
        display_obj.showAmountEntry(_pendingProduct.name, _pendingUnit, "");
    } else {
        _pendingExpiryDate = calcMHD(tmpl.shelfDays, 0);
        workflow = WorkflowMode::SAVING;
        state.setState(AppState::SAVING);
        finishStorageWorkflow();
    }
}

void App::addSorteToTemplate(const String &templateId, const String &sorte) {
    JsonDocument doc;
    if (!json.loadDocument("/custom_products.json", doc, "[]")) return;
    bool found = false;
    for (JsonObject obj : doc.as<JsonArray>()) {
        if (obj["id"].as<String>() == templateId) {
            JsonArray arr = obj["sorten"].is<JsonArray>()
                ? obj["sorten"].as<JsonArray>()
                : obj["sorten"].to<JsonArray>();
            arr.add(sorte);
            found = true;
            break;
        }
    }
    if (found) {
        json.saveDocument("/custom_products.json", doc);
        loadTemplates();  // refresh in-memory list with new sorte
    }
}

void App::removeSorteFromTemplate(const String &templateId, int sorteIdx) {
    JsonDocument doc;
    if (!json.loadDocument("/custom_products.json", doc, "[]")) return;
    bool changed = false;
    for (JsonObject obj : doc.as<JsonArray>()) {
        if (obj["id"].as<String>() == templateId) {
            if (!obj["sorten"].is<JsonArray>()) break;
            JsonArray arr = obj["sorten"].as<JsonArray>();
            int n = (int)arr.size();
            if (sorteIdx < 0 || sorteIdx >= n) break;
            // Rebuild array without the deleted element
            std::vector<String> kept;
            for (int i = 0; i < n; i++) {
                if (i != sorteIdx) kept.push_back(arr[i] | "");
            }
            obj.remove("sorten");
            JsonArray newArr = obj["sorten"].to<JsonArray>();
            for (const auto &s : kept) newArr.add(s);
            changed = true;
            break;
        }
    }
    if (changed) {
        json.saveDocument("/custom_products.json", doc);
        loadTemplates();
    }
}

void App::startTmplMHD() {
    auto products = templatesForCategory(_selectedCategory);
    if (_selectedTemplateIdx < 0 || _selectedTemplateIdx >= (int)products.size()) {
        workflow = WorkflowMode::HOME;
        renderActiveTab("Ungueltige Auswahl");
        return;
    }
    const ProductTemplate &tmpl = products[_selectedTemplateIdx];
    _pendingProduct.name    = tmpl.name;
    _pendingProduct.brand   = "";
    _pendingProduct.barcode = "";
    _pendingQuantity        = 1;
    _mhdOffset              = 0;
    _selectedBrand          = "";
    _pendingUnit            = tmpl.unit;
    _pendingAmountDraft     = "";
    _pendingAmount          = 1;
    _selectedSorte          = "";
    _creatingNewSorte       = false;

    if (tmpl.brands.size() > 1) {
        workflow = WorkflowMode::TMPL_BRAND;
        showTmplBrands();
    } else {
        _pendingProduct.brand = tmpl.brands.size() == 1 ? tmpl.brands[0] : "";
        if (tmpl.useSorten) {
            workflow = WorkflowMode::TMPL_SORTE;
            showTmplSorten();
        } else if (!tmpl.unit.isEmpty()) {
            workflow = WorkflowMode::TMPL_AMOUNT;
            display_obj.showAmountEntry(tmpl.name, tmpl.unit, "");
        } else {
            _pendingExpiryDate = calcMHD(tmpl.shelfDays, 0);
            workflow = WorkflowMode::SAVING;
            state.setState(AppState::SAVING);
            finishStorageWorkflow();
        }
    }
}

std::vector<String> App::loadLocationNames() const {
    std::vector<String> names;
    File f = AppFS::fs().open("/locations.json", "r");
    if (!f) return names;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok && doc.is<JsonArray>()) {
        for (JsonObject obj : doc.as<JsonArray>()) {
            String n = obj["name"] | "";
            if (!n.isEmpty()) names.push_back(n);
        }
    }
    f.close();
    return names;
}

String App::loadLocationColor(const String &name) const {
    File f = AppFS::fs().open("/locations.json", "r");
    if (!f) return "";
    JsonDocument doc;
    String color;
    if (deserializeJson(doc, f) == DeserializationError::Ok && doc.is<JsonArray>()) {
        for (JsonObject obj : doc.as<JsonArray>()) {
            if ((String)(obj["name"] | "") == name) {
                color = obj["color"] | "";
                break;
            }
        }
    }
    f.close();
    return color;
}

int App::countExpiringSoon(int days) const {
    // time_manager returns ISO (YYYY-MM-DD); convert to YYYYMMDD int for comparison
    auto isoToInt = [](const String &iso) -> long {
        if (iso.length() < 10) return 0;
        return iso.substring(0, 4).toInt() * 10000L
             + iso.substring(5, 7).toInt() * 100L
             + iso.substring(8, 10).toInt();
    };
    long todayInt = isoToInt(time_manager.today());
    long limitInt = isoToInt(time_manager.addDays(days));
    int n = 0;
    for (const auto &item : inventory.items()) {
        if (!item.expiryDate.isEmpty()) {
            long v = dmyToInt(item.expiryDate); // expiryDate is always DD.MM.YYYY
            if (v > 0 && v >= todayInt && v <= limitInt) n++;
        }
    }
    return n;
}

void App::showLocationSelect() {
    auto locs = loadLocationNames();
    display_obj.showLocationSelect(device_config.getActiveLocation(), locs);
}

void App::doInventoryPull() {
    if (!sync_manager.hasConfig() || !wifi_manager.isConnected()) return;
    const String &hh  = device_config.getHousehold();
    const String &dev = device_config.getDeviceName();

    // 1. Apply tombstones — remove items other devices deleted
    auto removals = sync_manager.pullRemovals(hh, dev);
    int removed = 0;
    for (const String &lb : removals) {
        if (inventory.hasLabel(lb)) {
            inventory.removeByLabelPermanent(lb);
            removed++;
        }
    }

    // 2. Merge new items from other devices
    auto pulled = sync_manager.pullInventory(hh, dev);
    int added = 0;
    for (const InventoryItem &item : pulled) {
        if (!inventory.hasLabel(item.labelBarcode)) {
            inventory.addItem(item);
            added++;
        }
    }

    if (removed || added)
        Logger::info("Sync", String("Pull: +") + added + " items, -" + removed + " removals");
}

void App::initWebServer() {
    Logger::info("Web", "Starting web server on port 80");
    web.setInventoryManager(&inventory);
    web.setJsonStorage(&json);
    web.setPrinterManager(&printer);
    web.begin();

    if (wifi_manager.isConnected())
        Logger::info("Web", "Station: http://" + wifi_manager.getIPAddress());
}

void App::handleSerialCommand(const String &command) {
    if (command.isEmpty()) return;

    if (command == "scan") {
        Logger::info("CMD", "Scanning WiFi");
        wifi_manager.scan();
    } else if (command.startsWith("ean ")) {
        String code = command.substring(4);
        code.trim();
        barcode_manager.scanner().injectCode(code);
        barcode_manager.scanner().injectCode("\n");
    } else if (command == "status") {
        Serial.println("\n=== Status ===");
        Serial.printf("State: %s\n", state.stateName());
        Serial.print("WiFi: ");
        Serial.println(wifi_manager.isConnected() ? "Connected" : "Offline/AP");
        Serial.print("IP: ");
        Serial.println(wifi_manager.getIPAddress());
        Serial.print("SSID: ");
        Serial.println(wifi_manager.getSSID());
        Serial.print("Inventory: ");
        Serial.println(static_cast<unsigned>(inventory.items().size()));
        Serial.print("Scanner: ");
        Serial.print(ble_scanner.getStatus());
        Serial.print(" ");
        Serial.println(ble_scanner.getDeviceName().isEmpty() ? ble_scanner.getDeviceAddress() : ble_scanner.getDeviceName());
        Serial.println();
    } else if (command == "beep") {
        audio_obj.playTone(1000, 100);
    } else if (command == "print") {
        size_t bytes = printer.printTestPage(false);
        Serial.printf("Printer ESC/POS test bytes=%u\n", static_cast<unsigned>(bytes));
    } else if (command == "printplain") {
        size_t bytes = printer.printPlainTest();
        Serial.printf("Printer plain test bytes=%u\n", static_cast<unsigned>(bytes));
    } else if (command == "printbaud") {
        size_t bytes = printer.printBaudProbe();
        Serial.printf("Printer baud probe bytes=%u\n", static_cast<unsigned>(bytes));
    } else if (command == "help") {
        Serial.println("\n=== Commands ===");
        Serial.println("scan       - Scan WiFi");
        Serial.println("ap         - AP Mode");
        Serial.println("ean <code> - Simulate barcode");
        Serial.println("status     - Status");
        Serial.println("beep       - Beep");
        Serial.println("print      - ESC/POS printer test");
        Serial.println("printplain - Plain UART printer test");
        Serial.println("printbaud  - Try common printer baudrates\n");
    } else {
        Logger::warn("CMD", String("Unknown command: ") + command);
    }
}
