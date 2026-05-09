#include "BLEScanner.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLEAdvertisedDevice.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <Preferences.h>

namespace {
constexpr uint16_t HID_SERVICE_UUID = 0x1812;
constexpr uint16_t HID_REPORT_UUID = 0x2A4D;
constexpr uint16_t HID_BOOT_KEYBOARD_INPUT_UUID = 0x2A22;
BLEUUID hidServiceUuid((uint16_t)HID_SERVICE_UUID);
BLEUUID hidReportUuid((uint16_t)HID_REPORT_UUID);
BLEUUID hidBootKeyboardInputUuid((uint16_t)HID_BOOT_KEYBOARD_INPUT_UUID);
BLEClient *client = nullptr;
BLEScanner *activeScanner = nullptr;

class ScannerClientCallbacks : public BLEClientCallbacks {
public:
    void onConnect(BLEClient *) override {}
    void onDisconnect(BLEClient *) override {
        if (activeScanner) activeScanner->disconnect();
    }
};

char keyToChar(uint8_t keycode, bool shift) {
    if (keycode >= 0x04 && keycode <= 0x1D) {
        char c = 'a' + (keycode - 0x04);
        return shift ? static_cast<char>(toupper(c)) : c;
    }
    if (keycode >= 0x1E && keycode <= 0x27) {
        const char normal[] = {'1','2','3','4','5','6','7','8','9','0'};
        const char shifted[] = {'!','@','#','$','%','^','&','*','(',')'};
        return shift ? shifted[keycode - 0x1E] : normal[keycode - 0x1E];
    }
    switch (keycode) {
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        case 0x2C: return ' ';
    }
    return 0;
}

} // namespace

BLEScanner ble_scanner;

// Arduino BLE notify callbacks are plain functions. Route them through the singleton.
static void scannerNotifyCallback(BLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
    ble_scanner.handleNotify(data, length);
}

void BLEScanner::begin() {
    if (!initialized) {
        BLEDevice::init("Lebensmittel-Scanner");
        initialized = true;
        activeScanner = this;
    }
    loadSettings();
    Serial.printf("[BLEScanner] Started. autoReconnect=%d savedDevice=%s\n",
                  autoReconnect, deviceAddress.c_str());
    Serial.flush();
}

void BLEScanner::loadSettings() {
    Preferences prefs;
    prefs.begin("scanner", true);
    autoReconnect = prefs.getBool("autoReconnect", true);
    deviceAddress = prefs.getString("addr", "");
    deviceName = prefs.getString("name", "");
    prefs.end();
}

void BLEScanner::saveSettings() {
    Preferences prefs;
    prefs.begin("scanner", false);
    prefs.putBool("autoReconnect", autoReconnect);
    prefs.putString("addr", deviceAddress);
    prefs.putString("name", deviceName);
    prefs.end();
}

std::vector<BLEScannerDevice> BLEScanner::scanDevices(uint32_t durationSeconds) {
    if (!initialized) begin();
    Serial.printf("[BLEScanner] Scanning BLE devices for %lus\n", static_cast<unsigned long>(durationSeconds));
    Serial.flush();

    std::vector<BLEScannerDevice> devices;

    class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    public:
        explicit ScanCallbacks(std::vector<BLEScannerDevice> &out) : devices(out) {}

        void onResult(BLEAdvertisedDevice dev) override {
            BLEScannerDevice item;
            item.address = dev.getAddress().toString().c_str();
            item.name = dev.haveName() ? dev.getName().c_str() : "";
            item.rssi = dev.getRSSI();
            item.hid = dev.haveServiceUUID() && dev.isAdvertisingService(hidServiceUuid);

            // Prefer HID devices but include named devices too because many barcode scanners do not advertise UUIDs.
            if (!item.hid && item.name.isEmpty()) return;
            for (auto &existing : devices) {
                if (existing.address == item.address) {
                    if (item.rssi > existing.rssi) existing = item;
                    return;
                }
            }
            devices.push_back(item);
            Serial.printf("[BLEScanner] Found %s '%s' RSSI=%d HID=%d\n",
                          item.address.c_str(), item.name.c_str(), item.rssi, item.hid);
        }

    private:
        std::vector<BLEScannerDevice> &devices;
    } callbacks(devices);

    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&callbacks, true);
    scan->setActiveScan(true);
    scan->setInterval(80);
    scan->setWindow(40);
    scan->start(durationSeconds, false);
    scan->clearResults();
    Serial.printf("[BLEScanner] Scan complete: %u candidate(s)\n", static_cast<unsigned>(devices.size()));
    Serial.flush();
    return devices;
}

bool BLEScanner::connectToDevice(const String &address, const String &name) {
    return connectInternal(address, name, true);
}

bool BLEScanner::connectInternal(const String &address, const String &name, bool persist) {
    if (!initialized) begin();
    if (address.isEmpty()) return false;

    connecting = true;
    connected = false;
    Serial.printf("[BLEScanner] Connecting to %s '%s'\n", address.c_str(), name.c_str());
    Serial.flush();

    if (client) {
        if (client->isConnected()) client->disconnect();
        delete client;
        client = nullptr;
    }

    client = BLEDevice::createClient();
    client->setClientCallbacks(new ScannerClientCallbacks(), true);

    bool ok = client->connect(BLEAddress(address.c_str()));
    if (!ok) {
        Serial.println("[BLEScanner] Connect failed");
        connecting = false;
        Serial.flush();
        return false;
    }

    BLERemoteService *service = client->getService(hidServiceUuid);
    if (!service) {
        Serial.println("[BLEScanner] HID service not found");
        client->disconnect();
        connecting = false;
        Serial.flush();
        return false;
    }

    BLERemoteCharacteristic *characteristic = service->getCharacteristic(hidReportUuid);
    if (!characteristic) characteristic = service->getCharacteristic(hidBootKeyboardInputUuid);

    if (!characteristic || !characteristic->canNotify()) {
        Serial.println("[BLEScanner] HID notify characteristic not found");
        client->disconnect();
        connecting = false;
        Serial.flush();
        return false;
    }

    characteristic->registerForNotify(scannerNotifyCallback);
    deviceAddress = address;
    if (!name.isEmpty()) deviceName = name;
    connected = true;
    connecting = false;
    if (persist) saveSettings();
    Serial.printf("[BLEScanner] Connected to %s '%s'\n", deviceAddress.c_str(), deviceName.c_str());
    Serial.flush();
    return true;
}

void BLEScanner::disconnect() {
    bool wasConnected = connected || connecting;
    connected = false;
    connecting = false;
    if (client && client->isConnected()) {
        client->disconnect();
    }
    if (wasConnected) {
        Serial.println("[BLEScanner] Disconnected");
        Serial.flush();
    }
}

void BLEScanner::setAutoReconnect(bool enabled) {
    autoReconnect = enabled;
    saveSettings();
}

void BLEScanner::loop() {
    if (client && connected && !client->isConnected()) {
        disconnect();
    }

    if (!autoReconnect || connected || connecting || deviceAddress.isEmpty()) return;
    uint32_t now = millis();
    if (now - lastReconnectAttempt < reconnectIntervalMs) return;
    lastReconnectAttempt = now;
    Serial.printf("[BLEScanner] Auto-reconnect to %s\n", deviceAddress.c_str());
    Serial.flush();
    connectInternal(deviceAddress, deviceName, false);
}

void BLEScanner::handleNotify(uint8_t *data, size_t length) {
    if (length < 3) return;
    uint8_t modifiers = data[0];
    for (size_t i = 2; i < length; i++) {
        uint8_t key = data[i];
        if (!key) continue;
        appendKey(key, modifiers);
    }
}

void BLEScanner::appendKey(uint8_t keycode, uint8_t modifiers) {
    if (keycode == 0x28 || keycode == 0x58) {
        finishCode();
        return;
    }
    bool shift = modifiers & 0x22;
    char c = keyToChar(keycode, shift);
    if (c) currentCode += c;
}

void BLEScanner::injectCode(const String &code) {
    if (code == "\n") {
        finishCode();
        return;
    }
    currentCode += code;
}

void BLEScanner::finishCode() {
    currentCode.trim();
    if (currentCode.isEmpty()) return;
    pendingCode = currentCode;
    lastScan = currentCode;
    Serial.printf("[BLEScanner] Scan: %s\n", lastScan.c_str());
    Serial.flush();
    currentCode = "";
}

bool BLEScanner::readCode(String &code) {
    if (pendingCode.isEmpty()) return false;
    code = pendingCode;
    pendingCode = "";
    return true;
}

String BLEScanner::getStatus() const {
    if (connected) return "connected";
    if (connecting) return "connecting";
    if (!deviceAddress.isEmpty() && autoReconnect) return "reconnecting";
    return "disconnected";
}
