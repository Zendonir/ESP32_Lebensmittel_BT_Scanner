#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <DNSServer.h>
#include "display.h"
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
    void renderDashboard(const String &message = "");
    void handleTouch();
    void processOnscreenAction(OnscreenAction action);

    TwoWire         i2c_bus;
    AppStateManager state;
    EventBus        events;
    TimeManager     time_manager;
    LittleFSManager fs;
    WebInterface    web;
    DNSServer       _dns;
    bool            _dnsRunning = false;
    bool            _touchWasPressed = false;
    uint32_t        _lastTouchMs = 0;
    uint32_t        _lastUiRefreshMs = 0;
    String          _statusMessage;
};

extern App app;
