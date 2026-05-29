#pragma once

#include <Arduino.h>
#include <vector>
#include <freertos/semphr.h>

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

    // Returns all found BLE devices after a blocking scan of durationSeconds.
    std::vector<BLEScannerDevice> scanDevices(uint32_t durationSeconds = 5);

    bool connectToDevice(const String &address, const String &name = "");
    void requestConnect(const String &address, const String &name = "");
    void disconnect();

    // Called from NimBLE callbacks – do not call directly
    void handleClientDisconnect();
    void handleNotify(uint8_t *data, size_t length);
    void _setConnected(bool c);
    void _setConnecting(bool c);
    void _startScan();
    String  _targetName; // filter for HID scan; empty = any HID device
    uint8_t _addrType = 0; // BLE_ADDR_PUBLIC=0, BLE_ADDR_RANDOM=1

    bool readCode(String &code);
    void injectCode(const String &code);

    void setAutoReconnect(bool enabled);
    bool getAutoReconnect() const { return autoReconnect; }

    void     setIdleTimeout(uint32_t minutes);
    uint32_t getIdleTimeoutMin() const { return _idleTimeoutMs / 60000; }

    bool isConnected()  const { return connected; }
    bool isConnecting() const { return connecting || connectRequested; }
    String getDeviceAddress()  const { return deviceAddress; }
    String getDeviceName()     const { return deviceName; }
    String getLastScan()       const { return lastScan; }
    String getLastError()      const { return lastError; }
    String getStatus()         const;
    int    getBatteryLevel()   const; // 0-100, or -1 if unknown
    void   readBatteryNow();          // manual refresh (call from loop periodically)

private:
    void loadSettings();
    void saveSettings();

    bool initialized      = false;
    bool autoReconnect    = true;
    volatile bool connected        = false;
    volatile bool connecting       = false;
    volatile bool connectRequested = false;
    volatile bool reconnectPaused  = false;  // written Core1, read Core0 (NimBLE CB)
    String deviceAddress;
    String deviceName;
    String requestedAddress;
    String requestedName;
    String pendingCode;
    String currentCode;
    String lastScan;
    String lastError;
    uint8_t  reconnectFailures = 0;
    uint32_t _idleTimeoutMs    = 0;   // 0 = disabled
    uint32_t _lastActivityMs   = 0;
    uint32_t _reconnectAfterMs = 0;  // millis() target for post-idle reconnect; 0 = not pending
    uint32_t _watchNoAddrMs    = 0;  // watchdog: last fire when address unknown
    uint32_t _watchAddrMs      = 0;  // watchdog: last fire when address known

    SemaphoreHandle_t _mutex = nullptr;
};

extern BLEScanner ble_scanner;
