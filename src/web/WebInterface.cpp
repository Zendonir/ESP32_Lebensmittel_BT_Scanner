#include "WebInterface.h"
#include "WebAssets.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>

WebInterface::WebInterface(uint16_t port) : server(port) {}

void WebInterface::begin() {
    registerRoutes();
    server.begin();
    Logger::info("Web", "Web interface started");
}

void WebInterface::registerRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/api/system-info", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["device"] = "lebensmittel-scanner";
        doc["heap"] = ESP.getFreeHeap();
        doc["psram"] = ESP.getFreePsram();
        String body;
        serializeJson(doc, body);
        request->send(200, "application/json", body);
    });
}
