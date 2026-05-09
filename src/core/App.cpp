#include "App.h"
#include "Logger.h"
#include "config.h"
#include "audio.h"
#include "wifi_manager.h"
#include "touch.h"
#include "display.h"

App app;

App::App() : i2c_bus(0), web(80) {}

void App::begin() {
    Logger::begin(115200);
    Logger::info("App", "ESP32-S3 Lebensmittel-Scanner booting");
    state.begin(AppState::BOOTING);

    initBacklight();

    Logger::info("Display", "Initializing ST7796");
    display_obj.init();
    display_obj.showSplash();

    initI2C();

    Logger::info("Touch", "Initializing FT6336");
    touch_obj.init(&i2c_bus);

    Logger::info("Audio", "Initializing audio feedback");
    audio_obj.init();
    audio_obj.playTone(1000, 120);

    state.setState(AppState::WIFI_CONNECTING);
    Logger::info("WiFi", "Initializing network manager");
    wifi_manager.init();
    renderWiFiStatus();

    state.setState(wifi_manager.isConnected() ? AppState::MAIN : AppState::AP_MODE);

    initFilesystem();

    Logger::info("Scanner", "Initializing BLE barcode scanner");
    barcode_manager.begin();

    initWebServer();

    Logger::info("App", "Ready. Serial commands: scan, ap, status, beep, help");
}

void App::loop() {
    if (_dnsRunning) _dns.processNextRequest();

    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        handleSerialCommand(command);
    }

    barcode_manager.loop();
    ScanResult scan;
    if (barcode_manager.readScan(scan)) {
        Logger::info("Scanner", String("Barcode: ") + scan.code);
        audio_obj.playTone(1800, 80);
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

void App::renderWiFiStatus() {
    String wifi_ssid = wifi_manager.getSSID();
    String wifi_ip = wifi_manager.getIPAddress();
    bool wifi_connected = wifi_manager.isConnected();
    display_obj.showWiFiStatus(wifi_ssid, wifi_ip, wifi_connected);
}

void App::initFilesystem() {
    Logger::info("LittleFS", "Mounting filesystem");
    if (!fs.begin()) {
        Logger::error("LittleFS", "Mount failed – web files unavailable");
    } else {
        Logger::info("LittleFS", "Mounted OK");
    }
}

void App::initWebServer() {
    // ── Diagnostic: one blocking scan to confirm the WiFi driver can scan ──
    Logger::info("WiFiScan", "Boot diagnostic scan starting…");
    int diagN = wifi_manager.scanNetworks(true);
    Logger::info("WiFiScan", String("Diagnostic result: ") + diagN + " networks");
    for (int i = 0; i < diagN && i < 8; i++) {
        Logger::info("WiFiScan",
            String("  ") + i + ": " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + " dBm)");
    }
    web.primeWiFiScanCache(diagN);
    WiFi.scanDelete();
    // ───────────────────────────────────────────────────────────────────────

    Logger::info("Web", "Starting web server on port 80");
    web.begin();

    // Start captive-portal DNS so devices connecting to the AP get redirected
    // to the WiFi setup page automatically.
    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    if (_dns.start(53, "*", WiFi.softAPIP())) {
        _dnsRunning = true;
        Logger::info("DNS", "Captive-portal DNS started");
    } else {
        Logger::warn("DNS", "DNS server start failed");
    }

    if (wifi_manager.isConnected()) {
        Logger::info("Web", "Station: http://" + wifi_manager.getIPAddress());
    }
    Logger::info("Web", "AP: http://" + WiFi.softAPIP().toString());
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
        renderWiFiStatus();
    } else if (command == "status") {
        Serial.println("\n=== Status ===");
        Serial.printf("State: %s\n", state.stateName());
        Serial.print("WiFi: ");
        Serial.println(wifi_manager.isConnected() ? "Connected" : "Offline/AP");
        Serial.print("IP: ");
        Serial.println(wifi_manager.getIPAddress());
        Serial.print("SSID: ");
        Serial.println(wifi_manager.getSSID());
        Serial.print("Scanner: ");
        Serial.print(ble_scanner.getStatus());
        Serial.print(" ");
        Serial.println(ble_scanner.getDeviceName().isEmpty() ? ble_scanner.getDeviceAddress() : ble_scanner.getDeviceName());
        Serial.println();
    } else if (command == "beep") {
        audio_obj.playTone(1000, 100);
    } else if (command == "help") {
        Serial.println("\n=== Commands ===");
        Serial.println("scan   - Scan WiFi");
        Serial.println("ap     - AP Mode");
        Serial.println("status - Status");
        Serial.println("beep   - Beep\n");
    } else {
        Logger::warn("CMD", String("Unknown command: ") + command);
    }
}
