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

    Logger::info("Touch", "Initializing FT6336");
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
    time_manager.begin(i2c_bus);
    device_config.begin();
    labelCounter.begin();
    inventory.begin();
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

    // Restore active location badge on display
    display_obj.setActiveLocation(device_config.getActiveLocation());

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
            display_obj.showListScreen("WLAN auswählen", _wifiNets, false);
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
            display_obj.showKeyboardEntry("Passwort falsch? Erneut:", "");
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
    handleTouch();
    processWorkflow();

    ScanResult scan;
    if (barcode_manager.readScan(scan)) {
        handleScan(scan);
    }

    AppEvent event;
    while (events.poll(event)) {
        Logger::debug("Event", String("Event received: ") + static_cast<int>(event.type));
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
    Logger::info("I2C", String("SDA=") + TOUCH_SDA + " SCL=" + TOUCH_SCL);
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

String App::buildUiSignature() const {
    return String(static_cast<int>(_activeTab)) + "|" +
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
}

void App::renderActiveTab(const String &message, bool force) {
    if (!message.isEmpty()) _statusMessage = message;

    String signature = buildUiSignature();
    if (!force && signature == _lastUiSignature) {
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
        _statusMessage,
        AppFS::usingSD());
    _lastUiSignature = signature;
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

    // Keyboard entry: char input (KB_ENTRY and WIFI_SETUP_PASS share the keyboard)
    if (action == OnscreenAction::KB_CHAR &&
            (workflow == WorkflowMode::KB_ENTRY || workflow == WorkflowMode::WIFI_SETUP_PASS)) {
        char c = display_obj.drainKbChar();
        if (c != 0) {
            audio_obj.playClickTone();
            _kbText += c;
            if (workflow == WorkflowMode::WIFI_SETUP_PASS) {
                display_obj.showKeyboardEntry("Passwort für " + _selectedSsid + ":", _kbText);
            } else {
                display_obj.kbAutoShift(c);
                display_obj.showKeyboardEntry("Produktname eingeben", _kbText);
            }
        }
        return;
    }

    if (action == OnscreenAction::KB_CAPS &&
            (workflow == WorkflowMode::KB_ENTRY || workflow == WorkflowMode::WIFI_SETUP_PASS)) {
        audio_obj.playClickTone();
        display_obj.kbToggleCaps();
        if (workflow == WorkflowMode::WIFI_SETUP_PASS)
            display_obj.showKeyboardEntry("Passwort für " + _selectedSsid + ":", _kbText);
        else
            display_obj.showKeyboardEntry("Produktname eingeben", _kbText);
        return;
    }

    if (action == OnscreenAction::KB_BACKSPACE && workflow == WorkflowMode::WIFI_SETUP_PASS) {
        if (!_kbText.isEmpty()) _kbText.remove(_kbText.length() - 1);
        display_obj.showKeyboardEntry("Passwort für " + _selectedSsid + ":", _kbText);
        return;
    }

    // Swipe-right = back inside WiFi setup or template workflow
    if (action == OnscreenAction::SWIPE_RIGHT) {
        if (workflow == WorkflowMode::WIFI_SETUP_PASS) {
            display_obj.showListScreen("WLAN auswählen", _wifiNets, false);
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
        if (workflow == WorkflowMode::TMPL_MHD) {
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
    }

    // Template list selection
    if (action >= OnscreenAction::LIST_ITEM_0 && action <= OnscreenAction::LIST_ITEM_6) {
        int idx = static_cast<int>(action) - static_cast<int>(OnscreenAction::LIST_ITEM_0);
        if (workflow == WorkflowMode::WIFI_SETUP_LIST) {
            if (idx >= 0 && idx < (int)_wifiNets.size()) {
                _selectedSsid = _wifiNets[idx];
                _kbText = "";
                display_obj.kbReset();
                display_obj.showKeyboardEntry("Passwort für " + _selectedSsid + ":", "");
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
                    workflow              = WorkflowMode::TMPL_MHD;
                    String mhd = calcMHD(tmpl.shelfDays, _mhdOffset);
                    display_obj.showTemplateMHD(tmpl.name, mhd, _pendingQuantity);
                }
            }
        } else if (workflow == WorkflowMode::LOCATION_SELECT) {
            auto locs = loadLocationNames();
            // idx 0 = "Kein Lagerort" (clear), then actual locations
            if (idx == 0) {
                device_config.setActiveLocation("");
                display_obj.setActiveLocation("");
            } else {
                int locIdx = idx - 1;
                if (locIdx < (int)locs.size()) {
                    device_config.setActiveLocation(locs[locIdx]);
                    display_obj.setActiveLocation(locs[locIdx]);
                }
            }
            audio_obj.playClickTone();
            workflow = WorkflowMode::HOME;
            renderActiveTab(String("Lagerort: ") + device_config.getActiveLocation());
            return;
        }
        return;
    }

    switch (action) {
        // ── Tab navigation ──────────────────────────────────────────────────
        case OnscreenAction::SWIPE_LEFT: {
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
            display_obj.showInventoryList(inventory.items());
            _lastUiRefreshMs = millis();
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
            display_obj.showKeyboardEntry("Manuelle Eingabe", "");
            break;
        case OnscreenAction::REFRESH:
            workflow = WorkflowMode::HOME;
            renderActiveTab("Anzeige aktualisiert");
            break;
        case OnscreenAction::WIFI_SETUP:
            Logger::info("UI", "Touch action: WiFi setup");
            WiFi.scanNetworks(/*async=*/true, /*hidden=*/true);
            workflow = WorkflowMode::WIFI_SETUP_SCAN;
            display_obj.showWifiScan();
            break;
        case OnscreenAction::SCANNER_RECONNECT:
            Logger::info("UI", "Touch action: scanner reconnect/disconnect");
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
            }
            break;
        case OnscreenAction::DATE_CONFIRM:
            if (workflow == WorkflowMode::ENTER_DATE && formatDateDraft(_pendingExpiryDate)) {
                workflow = WorkflowMode::ENTER_QTY;
                state.setState(AppState::ENTER_QTY);
                display_obj.showQuantityEntry(_pendingProduct, _pendingExpiryDate, _pendingQuantity);
            } else if (workflow == WorkflowMode::ENTER_DATE) {
                display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
                audio_obj.playTone(250, 160);
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
            _pendingQuantity = qty;
            if (workflow == WorkflowMode::ENTER_QTY)
                display_obj.showQuantityEntry(_pendingProduct, _pendingExpiryDate, _pendingQuantity);
            else if (workflow == WorkflowMode::TMPL_MHD) {
                auto products = templatesForCategory(_selectedCategory);
                if (_selectedTemplateIdx < (int)products.size()) {
                    String mhd = calcMHD(products[_selectedTemplateIdx].shelfDays, _mhdOffset);
                    display_obj.showTemplateMHD(_pendingProduct.name, mhd, _pendingQuantity);
                }
            }
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
                // Go back to category selection
                workflow = WorkflowMode::TMPL_CATEGORY;
                showTmplCategories();
            } else if (workflow == WorkflowMode::TMPL_BRAND) {
                // Go back to product selection
                workflow = WorkflowMode::TMPL_PRODUCT;
                showTmplProducts();
            } else if (workflow == WorkflowMode::TMPL_MHD) {
                // Go back to brand selection (if >1 brand) or product selection
                auto products = templatesForCategory(_selectedCategory);
                if (_selectedTemplateIdx >= 0 && _selectedTemplateIdx < (int)products.size()
                        && products[_selectedTemplateIdx].brands.size() > 1) {
                    workflow = WorkflowMode::TMPL_BRAND;
                    showTmplBrands();
                } else {
                    workflow = WorkflowMode::TMPL_PRODUCT;
                    showTmplProducts();
                }
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

        // ── Template MHD confirm ───────────────────────────────────────────
        case OnscreenAction::MHD_DAY_MINUS:
            if (workflow == WorkflowMode::TMPL_MHD) {
                _mhdOffset--;
                auto products = templatesForCategory(_selectedCategory);
                if (_selectedTemplateIdx < (int)products.size()) {
                    String mhd = calcMHD(products[_selectedTemplateIdx].shelfDays, _mhdOffset);
                    display_obj.showTemplateMHD(_pendingProduct.name, mhd, _pendingQuantity);
                }
            }
            break;
        case OnscreenAction::MHD_DAY_PLUS:
            if (workflow == WorkflowMode::TMPL_MHD) {
                _mhdOffset++;
                auto products = templatesForCategory(_selectedCategory);
                if (_selectedTemplateIdx < (int)products.size()) {
                    String mhd = calcMHD(products[_selectedTemplateIdx].shelfDays, _mhdOffset);
                    display_obj.showTemplateMHD(_pendingProduct.name, mhd, _pendingQuantity);
                }
            }
            break;
        case OnscreenAction::MHD_CONFIRM:
            if (workflow == WorkflowMode::TMPL_MHD) {
                auto products = templatesForCategory(_selectedCategory);
                if (_selectedTemplateIdx < (int)products.size()) {
                    _pendingExpiryDate = calcMHD(products[_selectedTemplateIdx].shelfDays, _mhdOffset);
                    workflow = WorkflowMode::SAVING;
                    state.setState(AppState::SAVING);
                    finishStorageWorkflow();
                }
            }
            break;

        // ── Keyboard entry confirm / backspace ─────────────────────────────
        case OnscreenAction::KB_CONFIRM:
            if (workflow == WorkflowMode::WIFI_SETUP_PASS) {
                wifi_manager.saveCredentials(_selectedSsid.c_str(), _kbText.c_str());
                wifi_manager.connectToWiFi(_selectedSsid.c_str(), _kbText.c_str());
                _wifiConnectStartMs = millis();
                workflow = WorkflowMode::WIFI_SETUP_CONN;
                display_obj.showResult("Verbinde…", _selectedSsid, false);
                break;
            }
            if (workflow == WorkflowMode::KB_ENTRY) {
                if (_kbText.isEmpty()) {
                    audio_obj.playWarningTone();
                    break;
                }
                _pendingProduct.name    = _kbText;
                _pendingProduct.brand   = "";
                _pendingProduct.barcode = "";
                _pendingDateDraft       = "";
                _pendingExpiryDate      = "";
                _pendingQuantity        = 1;
                workflow = WorkflowMode::ENTER_DATE;
                state.setState(AppState::ENTER_DATE);
                display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
            }
            break;
        case OnscreenAction::KB_BACKSPACE:
            if (workflow == WorkflowMode::KB_ENTRY && !_kbText.isEmpty()) {
                audio_obj.playClickTone();
                _kbText.remove(_kbText.length() - 1);
                if (_kbText.isEmpty()) display_obj.kbAutoShift(' '); // treat empty as after-space
                display_obj.showKeyboardEntry("Produktname eingeben", _kbText);
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

    if (workflow != WorkflowMode::HOME && workflow != WorkflowMode::RESULT) {
        Logger::warn("Scanner", "Ignoring scan while product workflow is active");
        audio_obj.playTone(250, 120);
        return;
    }

    if (scan.type == BarcodeType::LABEL) {
        state.setState(AppState::RETRIEVE);
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
        workflow = WorkflowMode::RESULT;
        _resultSuccess = removed;
        _resultTitle = removed ? "Ausgelagert" : "Nicht gefunden";
        _resultMessage = removed ? scan.code : "Label ist nicht im Inventar";
        display_obj.showResult(_resultTitle, _resultMessage, _resultSuccess);
        return;
    }

    // Check 48 h recently-removed buffer before a full product lookup
    const InventoryItem *recent = inventory.findRecent(scan.code);
    if (recent) {
        Logger::info("Scanner", String("48h-buffer hit for bc=") + scan.code +
                     " lb=" + recent->labelBarcode + " name=" + recent->name);
        InventoryItem reItem = *recent;
        inventory.addItem(reItem);
        audio_obj.playSuccessTone();
        {
            JsonDocument sdoc;
            sdoc["type"]         = "ADD";
            sdoc["barcode"]      = reItem.barcode;
            sdoc["name"]         = reItem.name;
            sdoc["brand"]        = reItem.brand;
            sdoc["category"]     = reItem.category;
            sdoc["expiryDate"]   = reItem.expiryDate;
            sdoc["addedDate"]    = reItem.addedDate;
            sdoc["quantity"]     = reItem.quantity;
            sdoc["labelBarcode"] = reItem.labelBarcode;
            sdoc["household"]    = device_config.getHousehold();
            sdoc["deviceName"]   = device_config.getDeviceName();
            sdoc["timestamp"]    = (long)time(nullptr);
            String sp; serializeJson(sdoc, sp);
            sync_manager.enqueue("ADD", sp);
        }
        workflow = WorkflowMode::RESULT;
        _resultSuccess = true;
        _resultTitle = "Wieder eingebucht";
        _resultMessage = reItem.name.isEmpty() ? scan.code : reItem.name;
        display_obj.showResult(_resultTitle, _resultMessage, true);
        return;
    }

    audio_obj.playSuccessTone();
    startProductLookup(scan.code);
}

void App::startProductLookup(const String &barcode) {
    _pendingBarcode = barcode;
    _pendingProduct = ProductInfo();
    _pendingDateDraft = "";
    _pendingExpiryDate = "";
    _pendingQuantity = 1;
    workflow = WorkflowMode::FETCHING_PRODUCT;
    state.setState(AppState::FETCHING);
    display_obj.showFetchingProduct(barcode);
}

void App::fetchTaskFn(void *param) {
    App *self = static_cast<App *>(param);
    self->_fetchOk = self->openFoodFacts.fetchProduct(self->_pendingBarcode, self->_fetchedProduct);
    self->_fetchDone = true;
    vTaskDelete(nullptr);
}

void App::processWorkflow() {
    if (workflow != WorkflowMode::FETCHING_PRODUCT) return;

    if (!_fetchStarted) {
        // Launch fetch on core 0 (network core) – UI loop continues on core 1
        _fetchStarted = true;
        _fetchDone    = false;
        _fetchOk      = false;
        _fetchedProduct = ProductInfo();
        xTaskCreatePinnedToCore(fetchTaskFn, "api_fetch", 12288, this, 2, nullptr, 0);
        return;
    }

    if (!_fetchDone) return; // Still fetching – keep rendering

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
    if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2024 || year > 2099) return false;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", day, month, year);
    formatted = buf;
    return true;
}

bool App::finishStorageWorkflow() {
    state.setState(AppState::PRINTING);

    InventoryItem item;
    item.barcode = _pendingProduct.barcode;
    item.name = _pendingProduct.name.isEmpty() ? "Lebensmittel" : _pendingProduct.name;
    item.brand = _pendingProduct.brand;
    item.category = "Barcode";
    item.expiryDate = _pendingExpiryDate;
    item.addedDate = time_manager.today();
    item.quantity = _pendingQuantity;
    item.labelBarcode = labelCounter.nextLabel();
    item.location = device_config.getActiveLocation();

    bool stored = inventory.addItem(item);
    if (stored) {
        JsonDocument sdoc;
        sdoc["type"]         = "ADD";
        sdoc["barcode"]      = item.barcode;
        sdoc["name"]         = item.name;
        sdoc["brand"]        = item.brand;
        sdoc["category"]     = item.category;
        sdoc["expiryDate"]   = item.expiryDate;
        sdoc["addedDate"]    = item.addedDate;
        sdoc["quantity"]     = item.quantity;
        sdoc["labelBarcode"] = item.labelBarcode;
        sdoc["household"]    = device_config.getHousehold();
        sdoc["deviceName"]   = device_config.getDeviceName();
        sdoc["location"]     = item.location;
        sdoc["timestamp"]    = (long)time(nullptr);
        String sp; serializeJson(sdoc, sp);
        sync_manager.enqueue("ADD", sp);
    }
    bool printed = stored && printer.printLabel(item);
    workflow = WorkflowMode::RESULT;
    state.setState(stored ? AppState::SUCCESS : AppState::ERROR);
    _resultSuccess = stored;
    _resultTitle = stored ? "Eingelagert" : "Speicherfehler";
    _resultMessage = stored
        ? String(item.labelBarcode) + (printed ? " gedruckt" : " gespeichert, Drucker?")
        : "Inventar konnte nicht gespeichert werden";
    display_obj.showResult(_resultTitle, _resultMessage, stored);
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
        t.shelfDays = obj["defaultDays"] | (obj["shelfDays"] | 14);
        t.brands.clear();
        if (obj["brands"].is<JsonArray>()) {
            for (JsonVariant b : obj["brands"].as<JsonArray>())
                t.brands.push_back(b.as<String>());
        } else if (obj["brand"].is<const char *>()) {
            String s = obj["brand"] | "";
            if (!s.isEmpty()) t.brands.push_back(s);
        }
        if (!t.name.isEmpty()) { _templates.push_back(t); idx++; }
    }
    Logger::info("Templates", String(_templates.size()) + " Vorlagen geladen");
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
    return time_manager.addDays(shelfDays + offset);
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
        items.push_back(p.name + " (" + p.shelfDays + " Tage)");
    display_obj.showListScreen(_selectedCategory.c_str(), items, true);
}

void App::showTmplBrands() {
    auto products = templatesForCategory(_selectedCategory);
    if (_selectedTemplateIdx < 0 || _selectedTemplateIdx >= (int)products.size()) return;
    const ProductTemplate &tmpl = products[_selectedTemplateIdx];
    display_obj.showListScreen(tmpl.name.c_str(), tmpl.brands, true);
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

    if (tmpl.brands.size() > 1) {
        workflow = WorkflowMode::TMPL_BRAND;
        showTmplBrands();
    } else {
        _pendingProduct.brand = tmpl.brands.size() == 1 ? tmpl.brands[0] : "";
        workflow              = WorkflowMode::TMPL_MHD;
        String mhd = calcMHD(tmpl.shelfDays, 0);
        display_obj.showTemplateMHD(tmpl.name, mhd, _pendingQuantity);
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
