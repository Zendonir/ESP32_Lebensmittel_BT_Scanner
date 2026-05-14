#include "App.h"
#include "Logger.h"
#include "config.h"
#include "audio.h"
#include "wifi_manager.h"
#include "touch.h"
#include "display.h"

#include <WiFi.h>

App app;

App::App()
    : i2c_bus(0),
      json(fs),
      inventoryStorage(json),
      inventory(inventoryStorage),
      openFoodFacts(api, &fs),
      web(80) {}

void App::begin() {
    Logger::begin(115200);
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
    audio_obj.init();
    audio_obj.playTone(1000, 120);

    state.setState(AppState::WIFI_CONNECTING);
    Logger::info("WiFi", "Initializing network manager");
    wifi_manager.init();

    state.setState(wifi_manager.isConnected() ? AppState::MAIN : AppState::AP_MODE);

    initFilesystem();
    time_manager.begin();
    labelCounter.begin();
    inventory.begin();
    printer.begin();

    Serial.flush();
    Logger::info("Scanner", "Initializing BLE barcode scanner");
    Serial.flush();
    barcode_manager.begin();
    renderDashboard("EAN scannen zum Einlagern");

    // Prime the WiFi scan cache before the web server opens for connections.
    // Only runs in AP mode (no station link). Uses the async scan API so the
    // display continues to render during the wait (max 5 s).
    if (!wifi_manager.isConnected()) {
        Logger::info("WiFiScan", "Priming network list for setup page...");
        WiFi.scanNetworks(/*async=*/true, /*hidden=*/true);
        uint32_t t0 = millis();
        while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && millis() - t0 < 5000) {
            delay(30); // touch task handles polling independently
        }
        int n = WiFi.scanComplete();
        if (n >= 0) {
            web.primeWiFiScanCache(n);
            Logger::info("WiFiScan", String(n) + " Netzwerke gefunden");
        } else {
            Logger::warn("WiFiScan", "Scan fehlgeschlagen oder Timeout");
        }
        WiFi.scanDelete();
    }

    initWebServer();
    renderDashboard("Bereit");

    Logger::info("App", "Ready. Serial commands: scan, ap, status, beep, print, printplain, printbaud, help");
}

void App::loop() {
    // Touch polling runs in its own FreeRTOS task (startTouchTask in begin()).
    // No tick() call needed here.

    // If WiFi just connected (e.g. after web-UI credential submit) and the AP
    // is still up, shut it down and stop the captive-portal DNS server.
    if (wifi_manager.isConnected() && wifi_manager.isAPActive()) {
        wifi_manager.stopAP();
        if (_dnsRunning) {
            _dns.stop();
            _dnsRunning = false;
            Logger::info("WiFi", "Connected – AP and DNS stopped");
        }
    }

    if (_dnsRunning) _dns.processNextRequest();

    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        handleSerialCommand(command);
    }

    barcode_manager.loop();
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

void App::initI2C() {
    Logger::info("I2C", String("SDA=") + TOUCH_SDA + " SCL=" + TOUCH_SCL);
    i2c_bus.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);
}

void App::resetLCDViaTCA9554() {
    // TCA9554 at I2C 0x20: EXIO1 (bit 1) drives the LCD reset line.
    // Reg 0x03 = Configuration (0=output). Reg 0x01 = Output Port.
    constexpr uint8_t TCA_ADDR   = 0x20;
    constexpr uint8_t REG_OUTPUT = 0x01;
    constexpr uint8_t REG_CONFIG = 0x03;
    constexpr uint8_t RST_BIT    = 0x02;  // EXIO1

    auto tca_write = [&](uint8_t reg, uint8_t val) {
        i2c_bus.beginTransmission(TCA_ADDR);
        i2c_bus.write(reg);
        i2c_bus.write(val);
        i2c_bus.endTransmission();
    };

    tca_write(REG_CONFIG, ~RST_BIT);     // EXIO1 = output, all others = input
    tca_write(REG_OUTPUT, 0xFF);         // EXIO1 high (idle)
    delay(10);
    tca_write(REG_OUTPUT, ~RST_BIT);     // EXIO1 low  (assert reset)
    delay(10);
    tca_write(REG_OUTPUT, 0xFF);         // EXIO1 high (release reset)
    delay(200);                          // ST7796 needs ~120 ms to wake
    Logger::info("Display", "TCA9554 LCD reset done");
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
        _statusMessage);
    _lastUiSignature = signature;
    _lastUiRefreshMs = millis();
}

void App::handleTouch() {
    // LVGL reads the touch IC via its own input-device driver (called inside
    // display_obj.tick()).  We only need to dequeue whatever action a button
    // callback posted into the single-slot queue.
    OnscreenAction action = display_obj.hitTest(0, 0);
    if (action != OnscreenAction::NONE) processOnscreenAction(action);
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
        if (_pendingDateDraft.length() < 6) _pendingDateDraft += digit;
        display_obj.showDateEntry(_pendingProduct, _pendingDateDraft);
        return;
    }

    switch (action) {
        case OnscreenAction::TAB_STORE:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::STORE;
            renderActiveTab("EAN scannen zum Einlagern");
            break;
        case OnscreenAction::TAB_INVENTORY:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::INVENTORY;
            if (_activeTab == UiTab::INVENTORY) display_obj.showInventoryList(inventory.items());
            _lastUiRefreshMs = millis();
            break;
        case OnscreenAction::TAB_SCANNER:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::SCANNER;
            renderActiveTab("Scanner verbinden im Web-UI oder per gespeicherter Kopplung");
            break;
        case OnscreenAction::TAB_SYSTEM:
            workflow = WorkflowMode::HOME;
            _activeTab = UiTab::SYSTEM;
            renderActiveTab("Systemstatus und Setup AP");
            break;
        case OnscreenAction::REFRESH:
            workflow = WorkflowMode::HOME;
            renderActiveTab("Anzeige aktualisiert");
            break;
        case OnscreenAction::START_AP:
            Logger::info("UI", "Touch action: start AP");
            wifi_manager.startAP();
            state.setState(AppState::AP_MODE);
            _activeTab = UiTab::SYSTEM;
            renderActiveTab("Setup-AP aktiv: 192.168.4.1");
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
        case OnscreenAction::QTY_MINUS:
            if (workflow == WorkflowMode::ENTER_QTY && _pendingQuantity > 1) {
                _pendingQuantity--;
                display_obj.showQuantityEntry(_pendingProduct, _pendingExpiryDate, _pendingQuantity);
            }
            break;
        case OnscreenAction::QTY_PLUS:
            if (workflow == WorkflowMode::ENTER_QTY && _pendingQuantity < 99) {
                _pendingQuantity++;
                display_obj.showQuantityEntry(_pendingProduct, _pendingExpiryDate, _pendingQuantity);
            }
            break;
        case OnscreenAction::QTY_CONFIRM:
            if (workflow == WorkflowMode::ENTER_QTY) {
                workflow = WorkflowMode::SAVING;
                state.setState(AppState::SAVING);
                finishStorageWorkflow();
            }
            break;
        case OnscreenAction::CANCEL:
            workflow = WorkflowMode::HOME;
            state.setState(AppState::MAIN);
            renderDashboard("Vorgang abgebrochen");
            break;
        case OnscreenAction::PRINTER_FEED_1:
            printer.feed(1);
            break;
        case OnscreenAction::PRINTER_FEED_5:
            printer.feed(5);
            break;
        case OnscreenAction::PRINTER_FEED_BACK_1:
        case OnscreenAction::PRINTER_FEED_BACK_5:
            break;
        default:
            break;
    }
}

void App::handleScan(const ScanResult &scan) {
    Logger::info("Scanner", String("Barcode: ") + scan.code);

    if (workflow != WorkflowMode::HOME && workflow != WorkflowMode::RESULT) {
        Logger::warn("Scanner", "Ignoring scan while product workflow is active");
        audio_obj.playTone(250, 120);
        return;
    }

    audio_obj.playTone(1800, 80);

    if (scan.type == BarcodeType::LABEL) {
        state.setState(AppState::RETRIEVE);
        bool removed = inventory.removeByLabel(scan.code);
        workflow = WorkflowMode::RESULT;
        _resultSuccess = removed;
        _resultTitle = removed ? "Ausgelagert" : "Nicht gefunden";
        _resultMessage = removed ? scan.code : "Label ist nicht im Inventar";
        display_obj.showResult(_resultTitle, _resultMessage, _resultSuccess);
        return;
    }

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

    bool stored = inventory.addItem(item);
    bool printed = stored && printer.printLabel(item);
    workflow = WorkflowMode::RESULT;
    state.setState(stored ? AppState::SUCCESS : AppState::ERROR);
    _resultSuccess = stored;
    _resultTitle = stored ? "Eingelagert" : "Speicherfehler";
    _resultMessage = stored
        ? String(item.labelBarcode) + (printed ? " gedruckt" : " gespeichert, Drucker?")
        : "Inventar konnte nicht gespeichert werden";
    display_obj.showResult(_resultTitle, _resultMessage, stored);
    audio_obj.playTone(stored ? 1800 : 240, stored ? 100 : 260);
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
    }
}

void App::initWebServer() {
    Logger::info("Web", "Starting web server on port 80");
    web.setInventoryManager(&inventory);
    web.setJsonStorage(&json);
    web.setPrinterManager(&printer);
    web.begin();

    // Only run captive-portal DNS when we are in AP mode (no station link).
    // When WiFi is connected the AP is down, so the DNS server is useless and
    // can interfere with normal DNS resolution on the local network.
    if (wifi_manager.isAPActive()) {
        _dns.setErrorReplyCode(DNSReplyCode::NoError);
        if (_dns.start(53, "*", WiFi.softAPIP())) {
            _dnsRunning = true;
            Logger::info("DNS", "Captive-portal DNS started (AP mode)");
        } else {
            Logger::warn("DNS", "DNS server start failed");
        }
        Logger::info("Web", "AP: http://" + WiFi.softAPIP().toString());
    } else {
        Logger::info("DNS", "Skipping captive-portal DNS (station mode)");
    }

    if (wifi_manager.isConnected()) {
        Logger::info("Web", "Station: http://" + wifi_manager.getIPAddress());
    }
}

void App::handleSerialCommand(const String &command) {
    if (command.isEmpty()) return;

    if (command == "scan") {
        Logger::info("CMD", "Scanning WiFi");
        wifi_manager.scan();
    } else if (command == "ap") {
        Logger::info("CMD", "Starting AP mode");
        wifi_manager.startAP();
        state.setState(AppState::AP_MODE);
        _activeTab = UiTab::SYSTEM;
        renderActiveTab("Setup-AP aktiv");
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
