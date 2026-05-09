#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class WebInterface {
public:
    explicit WebInterface(uint16_t port = 80);
    void begin();

private:
    AsyncWebServer server;
    void registerRoutes();
};
