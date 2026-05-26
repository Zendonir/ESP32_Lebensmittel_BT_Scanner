#include "BLEScanner.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

// ── Static singletons used by NimBLE callbacks ───────────────
static BLEScanner   *s_scanner    = nullptr;
static NimBLEClient *s_client     = nullptr;
static bool          s_nimInit    = false;
static int           s_batteryLevel = -1;  // 0-100, -1 = unknown

// ── Discovery list (shared between scan callbacks) ───────────
static std::vector<BLEScannerDevice> s_discovered;
static SemaphoreHandle_t s_discMutex  = nullptr;
static volatile bool     s_discovering = false;

// ── HID keycode → ASCII ──────────────────────────────────────
static char hidKey(uint8_t mod, uint8_t key) {
    bool shift = mod & 0x22;
    if (key >= 0x04 && key <= 0x1D) {
        char c = 'a' + (key - 0x04);
        return shift ? (char)(c - 32) : c;
    }
    if (key >= 0x1E && key <= 0x27) {
        const char *n = "1234567890", *s = "!@#$%^&*()";
        return shift ? s[key - 0x1E] : n[key - 0x1E];
    }
    switch (key) {
        case 0x28: return '\n';
        case 0x2C: return ' ';
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default:   return 0;
    }
}

// ── Battery level notify callback ───────────────────────────
static void batteryNotifyCB(NimBLERemoteCharacteristic *, uint8_t *data,
                             size_t len, bool) {
    if (len >= 1) {
        s_batteryLevel = (int)(uint8_t)data[0];
        Serial.printf("[BLE] Battery notify: %d%%\n", s_batteryLevel);
    }
}

// ── NimBLE HID notify callback ───────────────────────────────
static void hidNotifyCB(NimBLERemoteCharacteristic *, uint8_t *data,
                        size_t len, bool) {
    if (s_scanner && len >= 3) s_scanner->handleNotify(data, len);
}

// ── Client callbacks (disconnect → trigger reconnect scan) ───
class BLEClientCB : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient *, int reason) override {
        Serial.printf("[BLE] Disconnected (reason=%d)\n", reason);
        if (s_scanner) s_scanner->handleClientDisconnect();
    }
};
static BLEClientCB s_clientCB;

// ── Connection task (spawned from scan callback) ─────────────
static void connectTask(void *arg) {
    NimBLEAddress *addr = reinterpret_cast<NimBLEAddress *>(arg);

    if (!s_client) {
        s_client = NimBLEDevice::createClient();
        s_client->setClientCallbacks(&s_clientCB, false);
    }
    s_client->setConnectionParams(12, 12, 0, 51);

    Serial.printf("[BLE] Connecting to %s\n", addr->toString().c_str());
    if (!s_client->connect(*addr)) {
        delete addr;
        Serial.println("[BLE] Connect failed, restarting scan");
        if (s_scanner) {
            s_scanner->handleClientDisconnect();
            s_scanner->_startScan();
        }
        vTaskDelete(nullptr);
        return;
    }
    delete addr;
    Serial.println("[BLE] Connected, subscribing HID...");

    NimBLERemoteService *svc = s_client->getService(NimBLEUUID((uint16_t)0x1812));
    if (!svc) {
        Serial.println("[BLE] HID service not found");
        s_client->disconnect();
        if (s_scanner) { s_scanner->handleClientDisconnect(); s_scanner->_startScan(); }
        vTaskDelete(nullptr);
        return;
    }

    bool subscribed = false;
    const auto &chars = svc->getCharacteristics(true);
    for (auto *chr : chars) {
        if (chr->getUUID() == NimBLEUUID((uint16_t)0x2A4D) && chr->canNotify()) {
            if (chr->subscribe(true, hidNotifyCB)) subscribed = true;
        }
    }

    if (subscribed) {
        Serial.println("[BLE] HID subscribed OK");

        // Battery Service 0x180F / Battery Level 0x2A19
        s_batteryLevel = -1;
        NimBLERemoteService *battSvc = s_client->getService(NimBLEUUID((uint16_t)0x180F));
        if (battSvc) {
            NimBLERemoteCharacteristic *battChr =
                battSvc->getCharacteristic(NimBLEUUID((uint16_t)0x2A19));
            if (battChr) {
                if (battChr->canRead()) {
                    std::string val = battChr->readValue();
                    if (!val.empty()) {
                        s_batteryLevel = (int)(uint8_t)val[0];
                        Serial.printf("[BLE] Battery: %d%%\n", s_batteryLevel);
                    }
                }
                if (battChr->canNotify())
                    battChr->subscribe(true, batteryNotifyCB);
            }
        }

        if (s_scanner) s_scanner->_setConnected(true);
    } else {
        Serial.println("[BLE] No notifiable HID characteristic");
        s_client->disconnect();
        if (s_scanner) { s_scanner->handleClientDisconnect(); s_scanner->_startScan(); }
    }
    vTaskDelete(nullptr);
}

// ── HID-scanner scan callback (connects on HID device found) ─
class BLEScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        if (!s_scanner) return;
        const String &target = s_scanner->_targetName;
        if (!target.isEmpty()) {
            if (String(dev->getName().c_str()).indexOf(target) < 0) return;
        } else if (!dev->isAdvertisingService(NimBLEUUID((uint16_t)0x1812))) {
            return;
        }

        if (!s_discMutex) s_discMutex = xSemaphoreCreateMutex();
        BLEScannerDevice info;
        info.address = dev->getAddress().toString().c_str();
        info.name    = dev->getName().c_str();
        info.rssi    = dev->getRSSI();
        info.hid     = true;
        if (info.name.isEmpty()) info.name = "(kein Name)";

        xSemaphoreTake(s_discMutex, portMAX_DELAY);
        bool dup = false;
        for (auto &d : s_discovered) if (d.address == info.address) { dup = true; break; }
        if (!dup) s_discovered.push_back(info);
        xSemaphoreGive(s_discMutex);

        Serial.printf("[BLE] HID device found: %s '%s'\n", info.address.c_str(), info.name.c_str());
        NimBLEDevice::getScan()->stop();

        // Persist address type so requestConnect() can reconstruct correctly
        if (s_scanner) s_scanner->_addrType = dev->getAddress().getType();
        NimBLEAddress *a = new NimBLEAddress(dev->getAddress()); // copy with type
        s_scanner->_setConnecting(true);
        xTaskCreate(connectTask, "bleConn", 8192, a, 2, nullptr);
    }
};
static BLEScanCB s_scanCB;

// ── Discovery-only scan callback (all devices, no connect) ───
class BLEDiscoveryCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        if (!s_discMutex) s_discMutex = xSemaphoreCreateMutex();
        BLEScannerDevice info;
        info.address = dev->getAddress().toString().c_str();
        info.name    = dev->getName().c_str();
        info.rssi    = dev->getRSSI();
        info.hid     = dev->isAdvertisingService(NimBLEUUID((uint16_t)0x1812));
        if (info.name.isEmpty()) info.name = "(kein Name)";

        xSemaphoreTake(s_discMutex, portMAX_DELAY);
        bool dup = false;
        for (auto &d : s_discovered) if (d.address == info.address) { dup = true; break; }
        if (!dup) s_discovered.push_back(info);
        xSemaphoreGive(s_discMutex);
    }

    void onScanEnd(const NimBLEScanResults &, int) override {
        s_discovering = false;
        // Resume HID auto-connect scan
        if (s_scanner) {
            NimBLEDevice::getScan()->setScanCallbacks(&s_scanCB, false);
            s_scanner->_startScan();
        }
    }
};
static BLEDiscoveryCB s_discoveryCB;

// ═══════════════════════════════════════════════════════════
//   BLEScanner implementation
// ═══════════════════════════════════════════════════════════

BLEScanner ble_scanner;

void BLEScanner::begin() {
    if (!s_discMutex) s_discMutex = xSemaphoreCreateMutex();
    if (!_mutex)      _mutex      = xSemaphoreCreateMutex();

    if (!s_nimInit) {
        NimBLEDevice::init("FoodScanner");
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
        s_nimInit = true;
    }
    s_scanner = this;

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCB, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    loadSettings();
    initialized = true;

    Serial.printf("[BLE] NimBLE ready. autoReconnect=%d saved=%s '%s'\n",
                  autoReconnect, deviceAddress.c_str(), deviceName.c_str());

    if (autoReconnect && !deviceAddress.isEmpty()) {
        _targetName = deviceName;
        _startScan();
    }
}

void BLEScanner::loop() {
    // ── Idle timeout: disconnect if no scan received within _idleTimeoutMs ────
    if (connected && _idleTimeoutMs > 0
            && (millis() - _lastActivityMs) >= _idleTimeoutMs) {
        Serial.println("[BLE] Idle timeout – disconnecting");
        reconnectPaused   = true;              // block immediate reconnect
        _reconnectAfterMs = millis() + 60000;  // try again in 60 s
        _setConnected(false);
        if (s_client && s_client->isConnected()) s_client->disconnect();
    }

    // ── Delayed auto-reconnect after idle disconnect ───────────────────────
    if (!connected && !connecting && !connectRequested
            && _reconnectAfterMs != 0 && millis() >= _reconnectAfterMs) {
        _reconnectAfterMs = 0;
        reconnectPaused   = false;
        Serial.println("[BLE] Reconnect after idle timeout");
        _startScan();
    }

    // ── Scan-Watchdog: Scan jede Sekunde prüfen und ggf. neu starten ───────
    // Sichert schnelle Auto-Verbindung nach Boot oder wenn NimBLE den Scan
    // intern stoppt (z. B. Timing-Problem kurz nach begin()).
    {
        static uint32_t s_watchMs = 0;
        if (!connected && !connecting && !connectRequested
                && autoReconnect && !deviceAddress.isEmpty()
                && !reconnectPaused && !s_discovering
                && millis() - s_watchMs >= 1000) {
            s_watchMs = millis();
            if (!NimBLEDevice::getScan()->isScanning()) {
                Serial.println("[BLE] Watchdog: Scan gestoppt – starte neu");
                _startScan();
            }
        }
    }

    if (!connectRequested) return;
    connectRequested = false;

    // Stop any active scan before connecting
    if (NimBLEDevice::getScan()->isScanning())
        NimBLEDevice::getScan()->stop();

    deviceAddress = requestedAddress;
    if (!requestedName.isEmpty()) deviceName = requestedName;
    saveSettings();

    NimBLEAddress *a = new NimBLEAddress(std::string(requestedAddress.c_str()), _addrType);
    _setConnecting(true);
    xTaskCreate(connectTask, "bleConn", 8192, a, 2, nullptr);
}

void BLEScanner::_startScan() {
    if (s_discovering || reconnectPaused) return;
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan->isScanning()) return;
    scan->start(0, false, true); // continuous, non-blocking
}

void BLEScanner::_setConnected(bool c) {
    connected  = c;
    connecting = false;
    if (c) {
        reconnectFailures  = 0;
        reconnectPaused    = false;
        _reconnectAfterMs  = 0;
        _lastActivityMs    = millis();
        lastError          = "";
    } else {
        s_batteryLevel = -1;
    }
}

void BLEScanner::_setConnecting(bool c) {
    connecting = c;
    if (c) connected = false;
}

void BLEScanner::requestConnect(const String &address, const String &name) {
    if (address.isEmpty()) return;
    requestedAddress  = address;
    requestedName     = name;
    connectRequested  = true;
    reconnectPaused   = false;
    reconnectFailures = 0;
    lastError         = "";
}

bool BLEScanner::connectToDevice(const String &address, const String &name) {
    requestConnect(address, name);
    return true; // actual result comes asynchronously
}

void BLEScanner::disconnect() {
    connectRequested = false;
    reconnectPaused  = true;
    _setConnected(false);
    if (s_client && s_client->isConnected())
        s_client->disconnect();
    Serial.println("[BLE] Disconnected by request");
}

void BLEScanner::handleClientDisconnect() {
    bool was = connected || connecting;
    _setConnected(false);
    reconnectFailures++;
    if (was) Serial.println("[BLE] Client disconnected");
    if (autoReconnect && !reconnectPaused && initialized)
        _startScan();
}

std::vector<BLEScannerDevice> BLEScanner::scanDevices(uint32_t durationSeconds) {
    if (!initialized) begin();
    if (!s_discMutex) s_discMutex = xSemaphoreCreateMutex();

    xSemaphoreTake(s_discMutex, portMAX_DELAY);
    s_discovered.clear();
    xSemaphoreGive(s_discMutex);

    s_discovering = true;
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan->isScanning()) scan->stop();
    scan->setScanCallbacks(&s_discoveryCB, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    // Non-blocking start; block caller with vTaskDelay so RTOS can run
    scan->start(0, false, true);
    vTaskDelay(pdMS_TO_TICKS(durationSeconds * 1000));
    scan->stop();

    s_discovering = false;
    scan->setScanCallbacks(&s_scanCB, false);

    xSemaphoreTake(s_discMutex, portMAX_DELAY);
    auto result = s_discovered;
    xSemaphoreGive(s_discMutex);

    Serial.printf("[BLE] Discovery done: %d device(s)\n", (int)result.size());
    return result;
}

void BLEScanner::setAutoReconnect(bool enabled) {
    autoReconnect   = enabled;
    reconnectPaused = !enabled;
    if (enabled) reconnectFailures = 0;
    saveSettings();
}

void BLEScanner::setIdleTimeout(uint32_t minutes) {
    _idleTimeoutMs = minutes * 60000;
    saveSettings();
    Serial.printf("[BLE] Idle timeout set to %u min\n", minutes);
}

void BLEScanner::handleNotify(uint8_t *data, size_t len) {
    if (!_mutex || len < 3) return;
    _lastActivityMs = millis();  // reset idle timer on any HID input
    uint8_t mod = data[0];
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (size_t i = 2; i < len && i < 8; i++) {
        if (!data[i]) continue;
        char c = hidKey(mod, data[i]);
        if (c == '\n') {
            currentCode.trim();
            if (currentCode.length() > 3) {
                pendingCode = currentCode;
                lastScan    = currentCode;
                Serial.printf("[BLE] Scan: %s\n", lastScan.c_str());
            }
            currentCode = "";
        } else if (c) {
            currentCode += c;
        }
    }
    xSemaphoreGive(_mutex);
}

bool BLEScanner::readCode(String &code) {
    if (!_mutex) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool has = !pendingCode.isEmpty();
    if (has) { code = pendingCode; pendingCode = ""; }
    xSemaphoreGive(_mutex);
    return has;
}

void BLEScanner::injectCode(const String &raw) {
    if (!_mutex) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (raw == "\n") {
        currentCode.trim();
        if (currentCode.length() > 3) pendingCode = lastScan = currentCode;
        currentCode = "";
    } else {
        currentCode += raw;
    }
    xSemaphoreGive(_mutex);
}

String BLEScanner::getStatus() const {
    if (connected)                                  return "connected";
    if (connecting || connectRequested)             return "connecting";
    if (reconnectPaused)                            return "paused";
    if (!lastError.isEmpty())                       return "error";
    if (!deviceAddress.isEmpty() && autoReconnect)  return "reconnecting";
    return "disconnected";
}

int BLEScanner::getBatteryLevel() const { return s_batteryLevel; }

void BLEScanner::readBatteryNow() {
    if (!connected || !s_client || !s_client->isConnected()) return;
    NimBLERemoteService *battSvc = s_client->getService(NimBLEUUID((uint16_t)0x180F));
    if (!battSvc) return;
    NimBLERemoteCharacteristic *battChr =
        battSvc->getCharacteristic(NimBLEUUID((uint16_t)0x2A19));
    if (battChr && battChr->canRead()) {
        std::string val = battChr->readValue();
        if (!val.empty()) {
            s_batteryLevel = (int)(uint8_t)val[0];
            Serial.printf("[BLE] Battery refresh: %d%%\n", s_batteryLevel);
        }
    }
}

void BLEScanner::loadSettings() {
    Preferences p;
    if (!p.begin("scanner", false)) return;  // false = read-write, creates namespace on first boot
    autoReconnect  = p.getBool("autoReconnect", true);
    deviceAddress  = p.getString("addr", "");
    deviceName     = p.getString("name", "");
    _addrType      = (uint8_t)p.getUChar("addrType", 0);
    _idleTimeoutMs = (uint32_t)p.getUInt("idleMin", 0) * 60000;
    p.end();
}

void BLEScanner::saveSettings() {
    Preferences p;
    if (!p.begin("scanner", false)) return;
    p.putBool("autoReconnect", autoReconnect);
    p.putString("addr", deviceAddress);
    p.putString("name", deviceName);
    p.putUChar("addrType", _addrType);
    p.putUInt("idleMin", _idleTimeoutMs / 60000);
    p.end();
}
