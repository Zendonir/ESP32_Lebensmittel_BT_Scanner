#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Forward declarations – the web layer uses these managers via pointer; all are optional.
class InventoryManager;
class JsonStorage;
class PrinterManager;

class WebInterface {
public:
    explicit WebInterface(uint16_t port = 80);

    // Inject optional manager references before calling begin().
    void setInventoryManager(InventoryManager *mgr) { _invMgr = mgr; }
    void setJsonStorage(JsonStorage *storage)        { _storage = storage; }
    void setPrinterManager(PrinterManager *printer)  { _printer = printer; }

    void begin();
    void primeWiFiScanCache(int scanResult);

    // OTA combined status – abgefragt von App::loop() für Display-Anzeige
    bool        isOtaActive()          const;
    int         otaPct()               const;
    const char *otaPhase()             const;
    const char *otaTargetVersion()     const;

private:
    AsyncWebServer   _server;
    InventoryManager *_invMgr  = nullptr;
    JsonStorage      *_storage = nullptr;
    PrinterManager   *_printer = nullptr;

    void registerStaticRoutes();
    void registerApiRoutes();

    // Helpers
    static void sendJson(AsyncWebServerRequest *req, JsonDocument &doc, int code = 200);
    static void sendOk(AsyncWebServerRequest *req);
    static void sendError(AsyncWebServerRequest *req, const char *msg, int code = 500);

    // Body-parsing helpers (ESPAsyncWebServer passes body via AsyncWebServerRequest*)
    static bool parseBody(AsyncWebServerRequest *req, uint8_t *data, size_t len, JsonDocument &out);
};
