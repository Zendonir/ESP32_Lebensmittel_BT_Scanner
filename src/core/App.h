#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "AppStateManager.h"
#include "EventBus.h"
#include "TimeManager.h"

class App {
public:
    App();
    void begin();
    void loop();

private:
    void initBacklight();
    void initI2C();
    void handleSerialCommand(const String &command);
    void renderWiFiStatus();

    TwoWire i2c_bus;
    AppStateManager state;
    EventBus events;
    TimeManager time_manager;
};

extern App app;
