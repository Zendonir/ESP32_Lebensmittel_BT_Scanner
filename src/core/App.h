#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <DNSServer.h>
#include "AppStateManager.h"
#include "EventBus.h"
#include "TimeManager.h"
#include "../storage/LittleFSManager.h"
#include "../web/WebInterface.h"
#include "../scanner/BarcodeManager.h"

class App {
public:
    App();
    void begin();
    void loop();

private:
    void initBacklight();
    void initI2C();
    void initFilesystem();
    void initWebServer();
    void handleSerialCommand(const String &command);
    void renderWiFiStatus();

    TwoWire         i2c_bus;
    AppStateManager state;
    EventBus        events;
    TimeManager     time_manager;
    LittleFSManager fs;
    WebInterface    web;
    DNSServer       _dns;
    bool            _dnsRunning = false;
};

extern App app;
