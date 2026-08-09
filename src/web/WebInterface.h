#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <vector>

// Forward declarations – the web layer uses these managers via pointer; all are optional.
class InventoryManager;
class JsonStorage;
class PrinterManager;
class LabelCounter;

class WebInterface {
public:
    explicit WebInterface(uint16_t port = 80);

    // Inject optional manager references before calling begin().
    void setInventoryManager(InventoryManager *mgr) { _invMgr = mgr; }
    void setJsonStorage(JsonStorage *storage)        { _storage = storage; }
    void setPrinterManager(PrinterManager *printer)  { _printer = printer; }
    void setLabelCounter(LabelCounter *counter)      { _labels  = counter; }

    void begin();
    void primeWiFiScanCache(int scanResult);

    // OTA combined status – abgefragt von App::loop() für Display-Anzeige
    bool        isOtaActive()          const;
    int         otaPct()               const;
    const char *otaPhase()             const;
    const char *otaTargetVersion()     const;

    // Display-triggered OTA: fetch GitHub releases + start download
    void startReleasesFetch();
    bool isReleasesFetchDone()               const;
    bool isReleasesFetchOk()                 const;
    const std::vector<String> &getReleasesTags() const;
    const std::vector<String> &getReleasesUrls() const;
    bool startOtaFromUrl(const String &url, const String &version);

private:
    AsyncWebServer   _server;
    InventoryManager *_invMgr  = nullptr;
    JsonStorage      *_storage = nullptr;
    PrinterManager   *_printer = nullptr;
    LabelCounter     *_labels  = nullptr;

    void registerStaticRoutes();
    void registerApiRoutes();

    // Helpers
    static void sendJson(AsyncWebServerRequest *req, JsonDocument &doc, int code = 200);
    static void sendOk(AsyncWebServerRequest *req);
    static void sendError(AsyncWebServerRequest *req, const char *msg, int code = 500);

    // Body-parsing helpers (ESPAsyncWebServer passes body via AsyncWebServerRequest*)
    static bool parseBody(AsyncWebServerRequest *req, uint8_t *data, size_t len, JsonDocument &out);
};
