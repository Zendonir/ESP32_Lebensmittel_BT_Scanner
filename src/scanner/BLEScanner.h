#pragma once

#include <Arduino.h>
#include <vector>

struct BLEScannerDevice {
    String address;
    String name;
    int rssi = 0;
    bool hid = false;
};

class BLEScanner {
public:
    void begin();
    void loop();

    std::vector<BLEScannerDevice> scanDevices(uint32_t durationSeconds = 5);
    bool connectToDevice(const String &address, const String &name = "");
    void requestConnect(const String &address, const String &name = "");
    void disconnect();
    void handleClientDisconnect();

    bool readCode(String &code);
    void injectCode(const String &code);
    void handleNotify(uint8_t *data, size_t length);

    void setAutoReconnect(bool enabled);
    bool getAutoReconnect() const { return autoReconnect; }
    bool isConnected() const { return connected; }
    bool isConnecting() const { return connecting; }
    String getDeviceAddress() const { return deviceAddress; }
    String getDeviceName() const { return deviceName; }
    String getLastScan() const { return lastScan; }
    String getLastError() const { return lastError; }
    String getStatus() const;

private:
    void loadSettings();
    void saveSettings();
    void appendKey(uint8_t keycode, uint8_t modifiers);
    void finishCode();
    bool connectInternal(const String &address, const String &name, bool persist);
    void cleanupClient();

    bool initialized = false;
    bool autoReconnect = true;
    bool connected = false;
    bool connecting = false;
    bool connectRequested = false;
    String deviceAddress;
    String deviceName;
    String requestedAddress;
    String requestedName;
    String pendingCode;
    String currentCode;
    String lastScan;
    String lastError;
    uint32_t lastReconnectAttempt = 0;
    uint32_t reconnectIntervalMs = 10000;
};

extern BLEScanner ble_scanner;
