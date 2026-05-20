#include "WebInterface.h"
#include "../core/App.h"
#include "../core/Logger.h"
#include "../core/DeviceConfig.h"
#include "../storage/JsonStorage.h"
#include "../inventory/InventoryManager.h"
#include "../models/InventoryItem.h"
#include "wifi_manager.h"
#include "../scanner/BarcodeManager.h"
#include "../printer/PrinterManager.h"
#include "../network/SyncManager.h"
#include "audio.h"
#include "config.h"

#include <LittleFS.h>
#include "../storage/AppFS.h"
#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "../network/MySQLDirect.h"
#include <esp_system.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* ── OTA from URL ─────────────────────────────────────────────── */
static volatile int  _otaUrlPct  = 0;
static volatile bool _otaUrlDone = true;   // true = idle (not running)
static volatile bool _otaUrlOk   = false;

// Resolve up to 3 HTTP redirects manually and perform OTA download.
// GitHub release URLs redirect from github.com to objects.githubusercontent.com
// (cross-domain HTTPS). HTTPC_FORCE_FOLLOW_REDIRECTS can fail to re-init SSL
// for the new host, so we resolve the chain ourselves.
static void otaUrlTask(void *param) {
    char *urlBuf = static_cast<char *>(param);
    _otaUrlPct = 0; _otaUrlDone = false; _otaUrlOk = false;

    String currentUrl = urlBuf;
    free(urlBuf);

    // ── Step 1: follow redirects until we reach the real download URL ────────
    for (int hop = 0; hop < 4; hop++) {
        Logger::info("OTA", String("GET ") + currentUrl);
        WiFiClientSecure probe;
        probe.setInsecure();
        probe.setTimeout(20);
        HTTPClient http;
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.setTimeout(20000);
        http.setUserAgent("ESP32-OTA/1.0");
        if (!http.begin(probe, currentUrl)) {
            Logger::error("OTA", "http.begin failed");
            _otaUrlDone = true; vTaskDelete(nullptr); return;
        }
        int code = http.GET();
        Logger::info("OTA", String("HTTP ") + code);
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            String loc = http.getLocation();
            http.end();
            if (loc.isEmpty()) { Logger::error("OTA", "Empty redirect"); _otaUrlDone = true; vTaskDelete(nullptr); return; }
            currentUrl = loc;
            continue;
        }
        if (code != 200) {
            Logger::error("OTA", String("Unexpected HTTP ") + code);
            http.end(); _otaUrlDone = true; vTaskDelete(nullptr); return;
        }

        // ── Step 2: stream the body into the OTA partition ──────────────────
        int total = http.getSize();
        Logger::info("OTA", String("Size: ") + total + " bytes");
        if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
            Logger::error("OTA", "Update.begin failed");
            http.end(); _otaUrlDone = true; vTaskDelete(nullptr); return;
        }
        WiFiClient *stream = http.getStreamPtr();
        uint8_t buf[4096];
        int received = 0;
        while (http.connected() && (total < 0 || received < total)) {
            int avail = stream->available();
            if (!avail) { delay(5); continue; }
            int n = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
            if (n > 0) {
                Update.write(buf, n);
                received += n;
                if (total > 0) _otaUrlPct = received * 100 / total;
            }
        }
        bool ok = Update.end(true) && Update.isFinished();
        Logger::info("OTA", ok ? "OTA OK" : "OTA FAILED");
        http.end();
        _otaUrlOk = ok; _otaUrlDone = true;
        if (ok) {
            delay(200);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            delay(300);
            esp_restart();
        }
        vTaskDelete(nullptr);
        return;
    }
    Logger::error("OTA", "Too many redirects");
    _otaUrlDone = true;
    vTaskDelete(nullptr);
}

/* ── OTA LittleFS from URL ────────────────────────────────────── */
static volatile int  _otaFsPct  = 0;
static volatile bool _otaFsDone = true;
static volatile bool _otaFsOk   = false;

static void otaFsUrlTask(void *param) {
    char *urlBuf = static_cast<char *>(param);
    _otaFsPct = 0; _otaFsDone = false; _otaFsOk = false;

    String currentUrl = urlBuf;
    free(urlBuf);

    for (int hop = 0; hop < 4; hop++) {
        Logger::info("OTA", String("FS GET ") + currentUrl);
        WiFiClientSecure probe;
        probe.setInsecure();
        probe.setTimeout(20);
        HTTPClient http;
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.setTimeout(20000);
        http.setUserAgent("ESP32-OTA/1.0");
        if (!http.begin(probe, currentUrl)) {
            Logger::error("OTA", "FS http.begin failed");
            _otaFsDone = true; vTaskDelete(nullptr); return;
        }
        int code = http.GET();
        Logger::info("OTA", String("FS HTTP ") + code);
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            String loc = http.getLocation();
            http.end();
            if (loc.isEmpty()) { Logger::error("OTA", "FS empty redirect"); _otaFsDone = true; vTaskDelete(nullptr); return; }
            currentUrl = loc;
            continue;
        }
        if (code != 200) {
            Logger::error("OTA", String("FS unexpected HTTP ") + code);
            http.end(); _otaFsDone = true; vTaskDelete(nullptr); return;
        }

        int total = http.getSize();
        Logger::info("OTA", String("FS size: ") + total + " bytes");
        if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
            Logger::error("OTA", "FS Update.begin failed");
            http.end(); _otaFsDone = true; vTaskDelete(nullptr); return;
        }
        WiFiClient *stream = http.getStreamPtr();
        uint8_t buf[4096];
        int received = 0;
        while (http.connected() && (total < 0 || received < total)) {
            int avail = stream->available();
            if (!avail) { delay(5); continue; }
            int n = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
            if (n > 0) {
                Update.write(buf, n);
                received += n;
                if (total > 0) _otaFsPct = received * 100 / total;
            }
        }
        bool ok = Update.end(true) && Update.isFinished();
        Logger::info("OTA", ok ? "FS OTA OK" : "FS OTA FAILED");
        http.end();
        _otaFsOk = ok; _otaFsDone = true;
        vTaskDelete(nullptr);
        return;
    }
    Logger::error("OTA", "FS too many redirects");
    _otaFsDone = true;
    vTaskDelete(nullptr);
}

/* ── WiFi scan cache ──────────────────────────────────────────── */
static constexpr int WIFI_SCAN_CACHE_MAX = 30;

struct WiFiScanEntry {
    String ssid;
    int32_t rssi = 0;
    int32_t channel = 0;
    bool open = false;
};

/* ── BLE scan state ───────────────────────────────────────────── */
static volatile int  _bleScanState = 0; // 0=idle, 1=scanning, 2=done
static String        _bleScanJson;
static portMUX_TYPE  _bleScanMux = portMUX_INITIALIZER_UNLOCKED;

static bool _scanRunning = false;
static bool _scanReady = false;
static int _scanCount = 0;
static int _scanResult = WIFI_SCAN_RUNNING;
static uint32_t _scanUpdatedAt = 0;
static String _scanSource;
static WiFiScanEntry _scanCache[WIFI_SCAN_CACHE_MAX];
static bool loadJson(const char *path, JsonDocument &doc, const char *fallback);

static void resetScanCache() {
    for (int i = 0; i < WIFI_SCAN_CACHE_MAX; i++) {
        _scanCache[i] = WiFiScanEntry();
    }
    _scanCount = 0;
    _scanResult = WIFI_SCAN_RUNNING;
    _scanReady = false;
    _scanUpdatedAt = 0;
    _scanSource = "";
}

static int findCachedSSID(const String &ssid) {
    for (int i = 0; i < _scanCount; i++) {
        if (_scanCache[i].ssid == ssid) return i;
    }
    return -1;
}

static void cacheScanResults(int result, const char *source) {
    _scanResult = result;
    _scanSource = source ? source : "";
    _scanCount = 0;

    if (result > 0) {
        for (int i = 0; i < result; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue; // Hidden SSIDs cannot be selected usefully in the UI.

            int existing = findCachedSSID(ssid);
            int32_t rssi = WiFi.RSSI(i);
            if (existing >= 0) {
                // Keep the strongest BSSID for duplicate SSIDs.
                if (rssi <= _scanCache[existing].rssi) continue;
            } else {
                if (_scanCount >= WIFI_SCAN_CACHE_MAX) continue;
                existing = _scanCount++;
            }

            _scanCache[existing].ssid = ssid;
            _scanCache[existing].rssi = rssi;
            _scanCache[existing].channel = WiFi.channel(i);
            _scanCache[existing].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
    }

    _scanReady = true;
    _scanUpdatedAt = millis();
}


static void appendScanCache(JsonDocument &doc) {
    doc["scanReady"] = _scanReady;
    doc["scanResult"] = _scanResult;
    doc["scanCount"] = _scanCount;
    doc["scanAgeMs"] = _scanUpdatedAt ? millis() - _scanUpdatedAt : 0;
    doc["scanSource"] = _scanSource;

    JsonArray arr = doc["networks"].to<JsonArray>();
    if (_scanReady) {
        for (int i = 0; i < _scanCount; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = _scanCache[i].ssid;
            o["rssi"] = _scanCache[i].rssi;
            o["channel"] = _scanCache[i].channel;
            o["open"] = _scanCache[i].open;
        }
    }
}

static void sendScanResult(AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["scanning"] = _scanRunning;
    doc["ready"] = _scanReady;
    doc["result"] = _scanResult;
    doc["count"] = _scanCount;
    doc["ageMs"] = _scanUpdatedAt ? millis() - _scanUpdatedAt : 0;
    doc["source"] = _scanSource;

    if (_scanReady && _scanResult < 0) {
        doc["error"] = (_scanResult == WIFI_SCAN_FAILED) ? "wifi scan failed" : "wifi scan did not complete";
    }

    appendScanCache(doc);

    String body;
    serializeJson(doc, body);
    Serial.printf("[Web] %s -> scan cache ready=%d count=%d source=%s\n",
                  req->url().c_str(), _scanReady, _scanCount, _scanSource.c_str());
    Serial.flush();
    req->send(200, "application/json", body);
}

WebInterface::WebInterface(uint16_t port) : _server(port) {}

void WebInterface::begin() {
    if (_printer) {
        JsonDocument printerConfig;
        if (loadJson("/printer_config.json", printerConfig, "{}")) {
            uint32_t baud = printerConfig["baudrate"] | 0;
            if (baud > 0) _printer->configure(baud);
            uint16_t postFeed = (uint16_t)(printerConfig["postFeed"] | 100);
            _printer->setPostFeed(postFeed);
        }
    }

    registerStaticRoutes();
    registerApiRoutes();
    _server.begin();
    Logger::info("Web", "Web interface started on port 80");
}

void WebInterface::primeWiFiScanCache(int scanResult) {
    cacheScanResults(scanResult, "boot");
    Serial.printf("[Web] Primed WiFi scan cache from boot scan: raw=%d visible=%d\n",
                  _scanResult, _scanCount);
    Serial.flush();
}

/* ---------------------------------------------------------------
   Static file serving
   --------------------------------------------------------------- */
static const char SETUP_FALLBACK[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Setup</title>
<style>body{font-family:system-ui;background:#0f1117;color:#e8eaf6;margin:0;min-height:100vh;
display:flex;align-items:center;justify-content:center;padding:24px}
.box{background:#1a1d2e;border:1px solid #2a2d3e;border-radius:16px;padding:32px;max-width:500px;width:100%}
h1{color:#5c7cfa;margin:0 0 16px}p{color:#8892b0;line-height:1.6}
code{background:#0f1117;border:1px solid #2a2d3e;border-radius:6px;padding:10px 14px;display:block;
margin:10px 0;font-size:.9rem;color:#2dd4bf;word-break:break-all}
.step{display:flex;gap:12px;margin:12px 0;align-items:flex-start}
.num{background:#5c7cfa;color:#fff;border-radius:50%;width:26px;height:26px;display:flex;
align-items:center;justify-content:center;font-weight:700;flex-shrink:0}a{color:#5c7cfa}</style>
</head><body><div class="box"><h1>&#x26A0; Web-Dateien fehlen</h1>
<p>Der Webserver laeuft, aber LittleFS enthaelt noch keine Web-Dateien.</p>
<p><strong>Einmalig das Filesystem-Image flashen:</strong></p>
<div class="step"><div class="num">1</div>
<div>PlatformIO IDE:<br><em>Project Tasks &rarr; Platform &rarr; <strong>Upload Filesystem Image</strong></em></div></div>
<div class="step"><div class="num">2</div>
<div>oder Terminal:<code>pio run -e esp32-s3-devkitc1-n16r8 --target uploadfs</code></div></div>
<div class="step"><div class="num">3</div><div>Danach Seite neu laden.</div></div>
<p style="margin-top:20px"><a href="/api/ping">Ping (JSON)</a></p>
</div></body></html>)HTML";

static void sendNoCacheFile(AsyncWebServerRequest *req, const char *path, const char *contentType) {
    AsyncWebServerResponse *response = req->beginResponse(LittleFS, path, contentType);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    req->send(response);
}

// ETag derived from compile time — changes whenever firmware is rebuilt/reflashed.
// Browser caches static assets and revalidates with If-None-Match; server sends
// 304 (no body) when unchanged, which is nearly instant even over slow WiFi.
static const char *BUILD_ETAG = "\"" __DATE__ "-" __TIME__ "\"";

static void sendCachedFile(AsyncWebServerRequest *req, const char *path, const char *contentType) {
    const AsyncWebHeader *etag = req->getHeader("If-None-Match");
    if (etag && etag->value() == BUILD_ETAG) {
        req->send(304);
        return;
    }
    AsyncWebServerResponse *response = req->beginResponse(LittleFS, path, contentType);
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("ETag", BUILD_ETAG);
    req->send(response);
}

void WebInterface::registerStaticRoutes() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/index.html")) {
            sendCachedFile(req, "/index.html", "text/html");
        } else {
            req->send(200, "text/html", FPSTR(SETUP_FALLBACK));
        }
    });

    _server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *req) {
        static const char svg[] =
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
            "<rect width='32' height='32' rx='6' fill='#0d1117'/>"
            "<rect x='4'  y='6' width='2' height='20' fill='#4fc3f7'/>"
            "<rect x='7'  y='6' width='4' height='20' fill='#4fc3f7'/>"
            "<rect x='12' y='6' width='2' height='20' fill='#4fc3f7'/>"
            "<rect x='15' y='6' width='3' height='20' fill='#4fc3f7'/>"
            "<rect x='19' y='6' width='2' height='20' fill='#4fc3f7'/>"
            "<rect x='22' y='6' width='4' height='20' fill='#4fc3f7'/>"
            "<rect x='27' y='6' width='2' height='20' fill='#4fc3f7'/>"
            "</svg>";
        AsyncWebServerResponse *r = req->beginResponse(200, "image/svg+xml", svg);
        r->addHeader("Cache-Control", "max-age=86400");
        req->send(r);
    });
    _server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->redirect("/favicon.svg");
    });

    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/style.css"))
            sendCachedFile(req, "/style.css", "text/css");
        else
            req->send(404, "text/plain", "style.css not found – run uploadfs");
    });

    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/app.js"))
            sendCachedFile(req, "/app.js", "application/javascript");
        else
            req->send(404, "text/plain", "app.js not found – run uploadfs");
    });

    _server.on("/m", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/mobile.html"))
            sendCachedFile(req, "/mobile.html", "text/html");
        else
            req->send(404, "text/plain", "mobile.html not found – run uploadfs");
    });

    _server.onNotFound([](AsyncWebServerRequest *req) {
        const String &path = req->url();
        Serial.printf("[Web] 404/onNotFound %s host=%s\n", path.c_str(), req->host().c_str());
        Serial.flush();
        if (path.startsWith("/api/")) {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
            return;
        }
        // SPA fallback — all non-API routes load the app
        if (LittleFS.exists("/index.html"))
            sendCachedFile(req, "/index.html", "text/html");
        else
            req->send(404, "text/plain", "Not found");
    });
}

/* ---------------------------------------------------------------
   Helpers
   --------------------------------------------------------------- */
void WebInterface::sendJson(AsyncWebServerRequest *req, JsonDocument &doc, int code) {
    String body;
    serializeJson(doc, body);
    req->send(code, "application/json", body);
}

void WebInterface::sendOk(AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"ok\":true}");
}

void WebInterface::sendError(AsyncWebServerRequest *req, const char *msg, int code) {
    String body = "{\"error\":\"";
    body += msg;
    body += "\"}";
    req->send(code, "application/json", body);
}

/* Global body buffer — rebuilt per-request by bodyCollect() */
static String _body;

// Input validation helpers
static bool validStr(const String &s, size_t maxLen) {
    return s.length() <= maxLen;
}

static bool validName(const String &s) {
    return !s.isEmpty() && validStr(s, 128);
}

static bool validOptStr(const String &s, size_t maxLen = 256) {
    return validStr(s, maxLen);
}

static void bodyCollect(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                        size_t index, size_t total) {
    if (index == 0) {
        _body = "";
        _body.reserve(total > 0 ? total : 512);
    }
    _body += String(reinterpret_cast<char *>(data), len);
    // If server doesn't send Content-Length (chunked), trigger manually on non-empty chunk
    (void)req; (void)total;
}

static bool loadJson(const char *path, JsonDocument &doc,
                     const char *fallback = "{}") {
    File f = AppFS::fs().open(path, "r");
    if (!f) {
        deserializeJson(doc, fallback);
        return false;
    }
    bool ok = (deserializeJson(doc, f) == DeserializationError::Ok);
    f.close();
    if (!ok) deserializeJson(doc, fallback);
    return ok;
}

static bool saveJson(const char *path, JsonDocument &doc) {
    String tmp = String(path) + ".tmp";
    File f = AppFS::fs().open(tmp.c_str(), "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    AppFS::fs().remove(path);
    return AppFS::fs().rename(tmp.c_str(), path);
}

/* Serve a JSON file (GET) with fallback if missing */
static void sendFile(AsyncWebServerRequest *req, const char *path,
                     const char *fallback) {
    JsonDocument doc;
    loadJson(path, doc, fallback);
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
}


static void handleWiFiConnectRequest(AsyncWebServerRequest *req, const char *routeName) {
    JsonDocument inp;
    DeserializationError err = deserializeJson(inp, _body);
    if (err != DeserializationError::Ok) {
        Serial.printf("[API] %s invalid JSON: %s bodyLen=%u\n",
                      routeName, err.c_str(), static_cast<unsigned>(_body.length()));
        Serial.flush();
        req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    String ssid = inp["ssid"] | "";
    String pass = inp["password"] | "";
    ssid.trim();

    if (ssid.isEmpty()) {
        Serial.printf("[API] %s missing ssid bodyLen=%u\n", routeName, static_cast<unsigned>(_body.length()));
        Serial.flush();
        req->send(400, "application/json", "{\"error\":\"ssid required\"}");
        return;
    }

    Serial.printf("[API] %s connect ssid='%s' passLen=%u\n",
                  routeName, ssid.c_str(), static_cast<unsigned>(pass.length()));
    Serial.flush();
    wifi_manager.saveCredentials(ssid.c_str(), pass.c_str());
    wifi_manager.connectToWiFi(ssid.c_str(), pass.c_str());
    req->send(200, "application/json", "{\"ok\":true,\"connecting\":true}");
}

/* Merge POST body (_body) into an existing JSON object file */
static void mergePost(AsyncWebServerRequest *req, const char *path,
                      const char *fallback) {
    JsonDocument existing, incoming;
    loadJson(path, existing, fallback);
    if (deserializeJson(incoming, _body) == DeserializationError::Ok) {
        for (JsonPair kv : incoming.as<JsonObject>())
            existing[kv.key()] = kv.value();
    }
    saveJson(path, existing);
    req->send(200, "application/json", "{\"ok\":true}");
}

/* ---------------------------------------------------------------
   API Routes
   --------------------------------------------------------------- */
void WebInterface::registerApiRoutes() {

    // ---- PING ----
    _server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
        Serial.printf("[API] GET /api/ping host=%s\n", req->host().c_str());
        Serial.flush();
        String r = "{\"ok\":true,\"uptime\":";
        r += millis() / 1000;
        r += ",\"heap\":";
        r += ESP.getFreeHeap();
        r += "}";
        req->send(200, "application/json", r);
    });

    // ---- SYSTEM INFO ----
    _server.on("/api/system-info", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["firmware"]   = FW_VERSION;
        doc["household"]  = device_config.getHousehold();
        doc["deviceName"] = device_config.getDeviceName();
        doc["heap"]       = ESP.getFreeHeap();
        doc["heapTotal"]  = ESP.getHeapSize();
        doc["psram"]      = ESP.getFreePsram();
        doc["psramTotal"] = ESP.getPsramSize();
        doc["cpuFreq"]    = ESP.getCpuFreqMHz();
        doc["mac"]        = WiFi.macAddress();
        doc["ip"]         = WiFi.localIP().toString();
        const char *hn    = WiFi.getHostname();
        doc["hostname"]   = hn ? hn : "esp32-scanner";
        doc["fsUsed"]     = (uint32_t)AppFS::userDataUsedBytes();
        doc["fsTotal"]    = (uint32_t)AppFS::userDataTotalBytes();
        doc["sdMounted"]  = AppFS::usingSD();
        unsigned long s   = millis() / 1000;
        char up[32];
        snprintf(up, sizeof(up), "%lud %02luh %02lum",
                 s / 86400, (s % 86400) / 3600, (s % 3600) / 60);
        doc["uptime"] = up;
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

    // ---- DEVICE CONFIG (GET) ----
    _server.on("/api/device-config", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["household"]     = device_config.getHousehold();
        doc["householdAbbr"] = device_config.getHouseholdAbbr();
        doc["deviceName"]    = device_config.getDeviceName();
        doc["timezone"]      = device_config.getTimezone();
        doc["ntfyUrl"]       = device_config.getNtfyUrl();
        doc["ntfyTopic"]     = device_config.getNtfyTopic();
        doc["ntfyDays"]      = device_config.getNtfyDays();
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

    // ---- DEVICE CONFIG (POST) ----
    _server.on("/api/device-config", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument doc;
            if (deserializeJson(doc, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
                return;
            }
            if (!doc["household"].isNull()) {
                String v = doc["household"].as<String>();
                if (!validOptStr(v, 64)) { req->send(400, "application/json", "{\"error\":\"household zu lang\"}"); return; }
                device_config.setHousehold(v);
            }
            if (!doc["householdAbbr"].isNull()) {
                String v = doc["householdAbbr"].as<String>();
                if (!validOptStr(v, 8)) { req->send(400, "application/json", "{\"error\":\"householdAbbr zu lang (max 8)\"}"); return; }
                device_config.setHouseholdAbbr(v);
            }
            if (!doc["deviceName"].isNull()) {
                String v = doc["deviceName"].as<String>();
                if (!validOptStr(v, 64)) { req->send(400, "application/json", "{\"error\":\"deviceName zu lang\"}"); return; }
                device_config.setDeviceName(v);
            }
            if (!doc["timezone"].isNull()) {
                String v = doc["timezone"].as<String>();
                if (!validOptStr(v, 64)) { req->send(400, "application/json", "{\"error\":\"timezone zu lang\"}"); return; }
                device_config.setTimezone(v);
            }
            if (!doc["ntfyUrl"].isNull()) {
                String v = doc["ntfyUrl"].as<String>();
                if (!validOptStr(v, 256)) { req->send(400, "application/json", "{\"error\":\"ntfyUrl zu lang\"}"); return; }
                device_config.setNtfyUrl(v);
            }
            if (!doc["ntfyTopic"].isNull()) {
                String v = doc["ntfyTopic"].as<String>();
                if (!validOptStr(v, 128)) { req->send(400, "application/json", "{\"error\":\"ntfyTopic zu lang\"}"); return; }
                device_config.setNtfyTopic(v);
            }
            if (!doc["ntfyDays"].isNull())
                device_config.setNtfyDays(doc["ntfyDays"].as<int>());
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- INVENTORY (GET) ----
    _server.on("/api/inventory", HTTP_GET, [this](AsyncWebServerRequest *req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        if (_invMgr) {
            for (const auto &it : _invMgr->items()) {
                JsonObject obj = arr.add<JsonObject>();
                obj["barcode"]      = it.barcode;
                obj["name"]         = it.name;
                obj["brand"]        = it.brand;
                obj["category"]     = it.category;
                obj["expiryDate"]   = it.expiryDate;
                obj["addedDate"]    = it.addedDate;
                obj["quantity"]     = it.quantity;
                obj["labelBarcode"] = it.labelBarcode;
                obj["location"]     = it.location;
                obj["household"]    = device_config.getHousehold();
            }
        }
        sendJson(req, doc);
    });

    // ---- INVENTORY DELETE (register BEFORE /api/inventory POST — ESPAsyncWebServer prefix matching) ----
    _server.on("/api/inventory/delete", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            if (!_invMgr) { sendError(req, "Inventory nicht verfügbar"); return; }
            JsonDocument key;
            if (deserializeJson(key, _body) != DeserializationError::Ok) {
                sendError(req, "Ungültiges JSON", 400); return;
            }
            String lb = key["labelBarcode"] | "";
            String bc = key["barcode"]      | "";
            Serial.printf("[Web] DELETE inventory lb='%s' bc='%s'\n", lb.c_str(), bc.c_str());
            if (!lb.isEmpty())
                // Permanent delete: skip 48h buffer so the item is not re-created on next scan
                _invMgr->removeByLabelPermanent(lb);
            else if (!bc.isEmpty())
                _invMgr->removeByBarcode(bc);
            sendOk(req);
        },
        nullptr, bodyCollect);

    // ---- INVENTORY (POST – add or update) ----
    _server.on("/api/inventory", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            if (!_invMgr) { sendError(req, "Inventory nicht verfügbar"); return; }
            JsonDocument body;
            if (deserializeJson(body, _body) != DeserializationError::Ok) {
                sendError(req, "Ungültiges JSON", 400); return;
            }
            InventoryItem item;
            item.barcode      = body["barcode"]      | "";
            item.name         = body["name"]         | "";
            item.brand        = body["brand"]        | "";
            item.category     = body["category"]     | "";
            item.expiryDate   = body["expiryDate"]   | "";
            item.addedDate    = body["addedDate"]    | "";
            item.quantity     = body["quantity"]     | 1;
            item.labelBarcode = body["labelBarcode"] | "";

            // Validate fields
            if (!validOptStr(item.name, 128))       { sendError(req, "name zu lang", 400); return; }
            if (!validOptStr(item.brand, 64))        { sendError(req, "brand zu lang", 400); return; }
            if (!validOptStr(item.category, 64))     { sendError(req, "category zu lang", 400); return; }
            if (!validOptStr(item.barcode, 64))      { sendError(req, "barcode zu lang", 400); return; }
            if (!validOptStr(item.expiryDate, 16))   { sendError(req, "expiryDate ungültig", 400); return; }
            if (item.quantity < 1 || item.quantity > 999) { sendError(req, "quantity ungültig", 400); return; }

            if (!item.labelBarcode.isEmpty()) {
                // Edit path: update existing item in-place by labelBarcode
                Serial.printf("[Web] POST /api/inventory UPDATE lb='%s' name='%s'\n",
                              item.labelBarcode.c_str(), item.name.c_str());
                bool updated = _invMgr->updateByLabel(item.labelBarcode, item);
                if (!updated) {
                    // labelBarcode provided but not found: fall back to add
                    Serial.printf("[Web] updateByLabel not found, falling back to addItem\n");
                    _invMgr->addItem(item);
                }
            } else {
                // New item path: always append
                Serial.printf("[Web] POST /api/inventory ADD name='%s'\n", item.name.c_str());
                _invMgr->addItem(item);
            }
            sendOk(req);
        },
        nullptr, bodyCollect);

    // ---- CUSTOM PRODUCTS ----
    _server.on("/api/custom-products", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/custom_products.json", "[]");
    });
    // Sub-routes registered BEFORE /api/custom-products POST (prefix matching)
    _server.on("/api/custom-products/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/custom_products.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            if (deserializeJson(item, _body) == DeserializationError::Ok) {
                int idx = item["_idx"] | -1;
                item.remove("_idx");
                JsonArray a = arr.as<JsonArray>();
                int pos = 0;
                for (JsonVariant v : a) {
                    if (pos == idx) {
                        v.set(item.as<JsonObject>());
                        break;
                    }
                    pos++;
                }
                saveJson("/custom_products.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);
    _server.on("/api/custom-products/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, key, out;
            out.to<JsonArray>();
            loadJson("/custom_products.json", arr, "[]");
            if (deserializeJson(key, _body) == DeserializationError::Ok
                && arr.is<JsonArray>()) {
                int idx = key["_idx"] | -1;
                int pos = 0;
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if (pos != idx) out.as<JsonArray>().add(v);
                    pos++;
                }
                saveJson("/custom_products.json", out);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);
    _server.on("/api/custom-products", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/custom_products.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            if (deserializeJson(item, _body) == DeserializationError::Ok) {
                String name = item["name"] | "";
                int days = item["defaultDays"] | (item["shelfDays"] | 0);
                if (!validName(name)) {
                    req->send(400, "application/json", "{\"error\":\"name fehlt oder zu lang\"}"); return;
                }
                if (days <= 0) {
                    req->send(400, "application/json", "{\"error\":\"defaultDays muss > 0 sein\"}"); return;
                }
                arr.as<JsonArray>().add(item.as<JsonObject>());
                saveJson("/custom_products.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- CATEGORIES ----
    _server.on("/api/categories", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/categories.json", "[]");
    });
    // Sub-route registered BEFORE /api/categories POST (prefix matching)
    _server.on("/api/categories/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, key, out;
            out.to<JsonArray>();
            loadJson("/categories.json", arr, "[]");
            if (deserializeJson(key, _body) == DeserializationError::Ok
                && arr.is<JsonArray>()) {
                String name = key["name"] | "";
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if ((String)(v["name"] | "") == name) continue;
                    out.as<JsonArray>().add(v);
                }
                saveJson("/categories.json", out);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);
    _server.on("/api/categories", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/categories.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            if (deserializeJson(item, _body) == DeserializationError::Ok) {
                String oldName = item["oldName"] | "";
                item.remove("oldName");
                bool replaced = false;
                if (oldName.length()) {
                    for (JsonVariant v : arr.as<JsonArray>()) {
                        if ((String)(v["name"] | "") == oldName) {
                            v.set(item.as<JsonObject>());
                            replaced = true;
                            break;
                        }
                    }
                }
                if (!replaced) arr.as<JsonArray>().add(item.as<JsonObject>());
                saveJson("/categories.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- LAGERORTE (Storage Locations) ----
    _server.on("/api/locations", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/locations.json", "[]");
    });
    _server.on("/api/locations/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, key, out;
            out.to<JsonArray>();
            loadJson("/locations.json", arr, "[]");
            if (deserializeJson(key, _body) == DeserializationError::Ok
                && arr.is<JsonArray>()) {
                String name = key["name"] | "";
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if ((String)(v["name"] | "") == name) continue;
                    out.as<JsonArray>().add(v);
                }
                saveJson("/locations.json", out);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);
    _server.on("/api/locations", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/locations.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            if (deserializeJson(item, _body) == DeserializationError::Ok) {
                String oldName = item["oldName"] | "";
                item.remove("oldName");
                bool replaced = false;
                if (oldName.length()) {
                    for (JsonVariant v : arr.as<JsonArray>()) {
                        if ((String)(v["name"] | "") == oldName) {
                            v.set(item.as<JsonObject>());
                            replaced = true;
                            break;
                        }
                    }
                }
                if (!replaced) arr.as<JsonArray>().add(item.as<JsonObject>());
                saveJson("/locations.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);
    // Active location get/set
    _server.on("/api/active-location", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["location"] = device_config.getActiveLocation();
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    _server.on("/api/active-location", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument item;
            if (deserializeJson(item, _body) == DeserializationError::Ok) {
                String loc   = item["location"] | "";
                String color = item["color"]    | "";
                // If color not provided directly, look it up from locations.json
                if (color.isEmpty() && !loc.isEmpty()) {
                    JsonDocument arr;
                    loadJson("/locations.json", arr, "[]");
                    if (arr.is<JsonArray>()) {
                        for (JsonObject obj : arr.as<JsonArray>()) {
                            if ((String)(obj["name"] | "") == loc) {
                                color = obj["color"] | "";
                                break;
                            }
                        }
                    }
                }
                device_config.setActiveLocation(loc);
                device_config.setActiveLocationColor(color);
                display_obj.setActiveLocation(loc);
                display_obj.setActiveLocationColor(color);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- SHOPPING LIST ----
    _server.on("/api/shopping-list", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/shopping_list.json", "[]");
    });
    // Sub-routes registered BEFORE /api/shopping-list POST (prefix matching)
    _server.on("/api/shopping-list/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/shopping_list.json", arr, "[]");
            if (deserializeJson(item, _body) == DeserializationError::Ok
                && arr.is<JsonArray>()) {
                String name = item["name"] | "";
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if ((String)(v["name"] | "") == name) {
                        v["bought"] = item["bought"];
                        break;
                    }
                }
                saveJson("/shopping_list.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);
    _server.on("/api/shopping-list/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, key, out;
            out.to<JsonArray>();
            loadJson("/shopping_list.json", arr, "[]");
            if (deserializeJson(key, _body) == DeserializationError::Ok
                && arr.is<JsonArray>()) {
                String name = key["name"] | "";
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if ((String)(v["name"] | "") == name) continue;
                    out.as<JsonArray>().add(v);
                }
                saveJson("/shopping_list.json", out);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);
    _server.on("/api/shopping-list", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/shopping_list.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            if (deserializeJson(item, _body) == DeserializationError::Ok)
                arr.as<JsonArray>().add(item.as<JsonObject>());
            saveJson("/shopping_list.json", arr);
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- CONFIG FILES (simple GET/POST) ----
    auto cfgGet = [](const char *file) {
        return [file](AsyncWebServerRequest *req) { sendFile(req, file, "{}"); };
    };
    auto cfgPost = [](const char *file) {
        return [file](AsyncWebServerRequest *req) { mergePost(req, file, "{}"); };
    };

    _server.on("/api/ui-config",     HTTP_GET,  cfgGet("/ui_config.json"));
    // /reset before /api/ui-config POST (prefix matching)
    _server.on("/api/ui-config/reset", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            AppFS::fs().remove("/ui_config.json");
            req->send(200, "application/json", "{\"ok\":true}");
        });
    _server.on("/api/ui-config",     HTTP_POST, cfgPost("/ui_config.json"), nullptr, bodyCollect);

    _server.on("/api/font-config",   HTTP_GET,  cfgGet("/font_config.json"));
    _server.on("/api/font-config",   HTTP_POST, cfgPost("/font_config.json"), nullptr, bodyCollect);

    _server.on("/api/display-config", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        if (AppFS::fs().exists("/display_config.json")) {
            File f = AppFS::fs().open("/display_config.json", "r");
            if (f) { deserializeJson(doc, f); f.close(); }
        }
        if (!doc["standby_sec"].is<uint32_t>()) doc["standby_sec"] = 0;
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    _server.on("/api/display-config", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            mergePost(req, "/display_config.json", "{}");
            app.loadDisplayConfig();
        },
        nullptr, bodyCollect);

    _server.on("/api/printer-config", HTTP_GET, [this](AsyncWebServerRequest *req) {
        JsonDocument doc;
        loadJson("/printer_config.json", doc, "{}");
        if (!doc["baudrate"].is<uint32_t>()) doc["baudrate"] = _printer ? _printer->baud() : UART_BAUD;
        if (!doc["postFeed"].is<int>())      doc["postFeed"]  = 86;
        doc["ready"] = _printer && _printer->isReady();
        doc["txPin"] = UART_TX;
        doc["rxPin"] = UART_RX;
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    _server.on("/api/printer-config", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            JsonDocument incoming;
            if (deserializeJson(incoming, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
                return;
            }
            uint32_t baud     = incoming["baudrate"] | UART_BAUD;
            uint16_t postFeed = (uint16_t)(incoming["postFeed"] | 100);
            Logger::info("Printer", String("Web config update baud=") + baud + " postFeed=" + postFeed);
            mergePost(req, "/printer_config.json", "{}");
            if (_printer) {
                _printer->configure(baud);
                _printer->setPostFeed(postFeed);
            }
        },
        nullptr, bodyCollect);

    _server.on("/api/scanner-config", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        loadJson("/scanner_config.json", doc, "{}");
        doc["mode"] = "ble_hid";
        doc["autoReconnect"] = ble_scanner.getAutoReconnect();
        doc["bleAddress"] = ble_scanner.getDeviceAddress();
        doc["bleDevice"] = ble_scanner.getDeviceName().isEmpty()
            ? ble_scanner.getDeviceAddress()
            : ble_scanner.getDeviceName();
        doc["bleStatus"] = ble_scanner.getStatus();
        doc["bleConnected"] = ble_scanner.isConnected();
        doc["bleConnecting"] = ble_scanner.isConnecting();
        doc["bleLastError"] = ble_scanner.getLastError();
        doc["idleTimeoutMin"] = ble_scanner.getIdleTimeoutMin();
        doc["lastScan"] = barcode_manager.getLastScan().isEmpty()
            ? ble_scanner.getLastScan()
            : barcode_manager.getLastScan();
        doc["lastType"] = barcode_manager.getLastType();
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    _server.on("/api/scanner-config", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument incoming;
            if (deserializeJson(incoming, _body) == DeserializationError::Ok) {
                if (!incoming["autoReconnect"].isNull()) {
                    ble_scanner.setAutoReconnect(incoming["autoReconnect"].as<bool>());
                }
                if (!incoming["idleTimeoutMin"].isNull()) {
                    ble_scanner.setIdleTimeout(incoming["idleTimeoutMin"].as<uint32_t>());
                }

                JsonDocument existing;
                loadJson("/scanner_config.json", existing, "{}");
                existing["mode"] = "ble_hid";
                existing["autoReconnect"] = ble_scanner.getAutoReconnect();
                existing["idleTimeoutMin"] = ble_scanner.getIdleTimeoutMin();
                existing["bleAddress"] = ble_scanner.getDeviceAddress();
                existing["bleDevice"] = ble_scanner.getDeviceName();
                saveJson("/scanner_config.json", existing);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    _server.on("/api/mqtt",          HTTP_GET,  cfgGet("/mqtt_config.json"));
    // /mqtt/test before /api/mqtt POST (prefix matching)
    _server.on("/api/mqtt/test",     HTTP_POST,
        [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":false,\"message\":\"MQTT nicht konfiguriert\"}"); });
    _server.on("/api/mqtt",          HTTP_POST, cfgPost("/mqtt_config.json"), nullptr, bodyCollect);

    _server.on("/api/telegram",      HTTP_GET,  cfgGet("/telegram_config.json"));
    // /telegram/test before /api/telegram POST (prefix matching)
    _server.on("/api/telegram/test", HTTP_POST,
        [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":false,\"message\":\"Telegram nicht konfiguriert\"}"); });
    _server.on("/api/telegram",      HTTP_POST, cfgPost("/telegram_config.json"), nullptr, bodyCollect);

    // Sub-routes MUST be registered before the base "/api/server-sync" route because
    // ESPAsyncWebServer uses prefix matching — POST /api/server-sync would otherwise
    // intercept /api/server-sync/setup, /api/server-sync/test etc.
    _server.on("/api/server-sync/test", HTTP_POST, [](AsyncWebServerRequest *req) {
        String msg;
        bool ok = sync_manager.testConnection(msg);
        JsonDocument doc;
        doc["ok"]      = ok;
        doc["message"] = msg;
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    }, nullptr, bodyCollect);
    _server.on("/api/server-sync/queue", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", sync_manager.getQueueJson());
    });
    _server.on("/api/server-sync/queue/clear", HTTP_POST, [](AsyncWebServerRequest *req) {
        sync_manager.clearQueue();
        req->send(200, "application/json", "{\"ok\":true}");
    }, nullptr, bodyCollect);
    // Setup wizard: connect directly to MySQL as root, create DB/tables/user, then store
    // the sync credentials. Root credentials are NEVER stored on this device.
    _server.on("/api/server-sync/setup", HTTP_POST, [](AsyncWebServerRequest *req) {
        Serial.println("[SetupWizard] *** HANDLER ENTERED ***");
        Serial.printf("[SetupWizard] POST /api/server-sync/setup called, bodyLen=%u\n",
                      static_cast<unsigned>(_body.length()));
        Serial.flush();

        JsonDocument inp;
        DeserializationError derr = deserializeJson(inp, _body);
        if (derr != DeserializationError::Ok) {
            Serial.printf("[SetupWizard] JSON parse failed: %s\n", derr.c_str());
            Serial.flush();
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"Ungültige JSON-Daten\"}");
            return;
        }
        String host     = inp["host"]     | "";
        String rootUser = inp["rootUser"] | "";
        String rootPass = inp["rootPass"] | "";
        String syncPass = inp["syncPass"] | "";
        Serial.printf("[SetupWizard] Fields: host='%s' rootUser='%s' rootPassLen=%u syncPassLen=%u\n",
                      host.c_str(), rootUser.c_str(),
                      static_cast<unsigned>(rootPass.length()),
                      static_cast<unsigned>(syncPass.length()));
        Serial.flush();

        if (host.isEmpty() || rootUser.isEmpty() || rootPass.isEmpty() || syncPass.isEmpty()) {
            Serial.printf("[SetupWizard] Validation failed: host=%d rootUser=%d rootPass=%d syncPass=%d\n",
                          (int)!host.isEmpty(), (int)!rootUser.isEmpty(),
                          (int)!rootPass.isEmpty(), (int)!syncPass.isEmpty());
            Serial.flush();
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"host, rootUser, rootPass und syncPass erforderlich\"}");
            return;
        }
        wl_status_t wifiSt = WiFi.status();
        Serial.printf("[SetupWizard] WiFi status=%d connected=%d\n",
                      static_cast<int>(wifiSt), wifiSt == WL_CONNECTED);
        Serial.flush();
        if (wifiSt != WL_CONNECTED) {
            req->send(200, "application/json", "{\"ok\":false,\"error\":\"Kein WLAN – Gerät muss mit dem Heimnetz verbunden sein\"}");
            return;
        }

        Serial.printf("[SetupWizard] Connecting to MySQL at %s:3306\n", host.c_str());
        Serial.flush();

        // Run MySQL setup synchronously — direct TCP to MariaDB/MySQL takes ~2s,
        // well within the watchdog limit, and ESPAsyncWebServer requires req->send()
        // to be called before onRequest() returns (calling it from another task
        // causes a "Handler did not handle the request" auto-response from the server).
        MySQLDirect db;
        bool connected = db.connect(host, 3306, rootUser, rootPass, 4000);
        Serial.printf("[SetupWizard] MySQL connect: %s\n",
                      connected ? "OK" : db.lastError().c_str());
        Serial.flush();

        if (!connected) {
            String err = String("{\"ok\":false,\"error\":\"MySQL-Verbindung fehlgeschlagen: ")
                         + db.lastError() + "\"}";
            req->send(200, "application/json", err);
            return;
        }

        String lastErr;
        auto execSQL = [&](const String &sql) -> bool {
            Serial.printf("[SetupWizard] SQL: %.120s\n", sql.c_str());
            Serial.flush();
            bool ok = db.execute(sql);
            Serial.printf("[SetupWizard] result: %s\n", ok ? "OK" : db.lastError().c_str());
            Serial.flush();
            if (!ok && lastErr.isEmpty()) lastErr = db.lastError();
            return ok;
        };

        bool ok = true;

        ok = ok && execSQL(
            "CREATE DATABASE IF NOT EXISTS `Lebensmittel_Scanner` "
            "CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");

        ok = ok && execSQL(
            "CREATE TABLE IF NOT EXISTS `Lebensmittel_Scanner`.`inventar` ("
            "`id` INT AUTO_INCREMENT PRIMARY KEY,"
            "`label_barcode` VARCHAR(60) NOT NULL UNIQUE,"
            "`barcode` VARCHAR(60),`name` VARCHAR(200),`brand` VARCHAR(100),"
            "`category` VARCHAR(100),`expiry_date` VARCHAR(20),"
            "`added_date` VARCHAR(20),`quantity` INT DEFAULT 1,"
            "`household` VARCHAR(100),`device_name` VARCHAR(100),"
            "`location` VARCHAR(100) DEFAULT NULL,"
            "`synced_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
        // Migration for existing installations without the location column
        execSQL("ALTER TABLE `Lebensmittel_Scanner`.`inventar` "
                "ADD COLUMN IF NOT EXISTS `location` VARCHAR(100) DEFAULT NULL");

        ok = ok && execSQL(
            "CREATE TABLE IF NOT EXISTS `Lebensmittel_Scanner`.`sync_log` ("
            "`id` INT AUTO_INCREMENT PRIMARY KEY,`event_type` VARCHAR(30),"
            "`label_barcode` VARCHAR(60),`barcode` VARCHAR(60),"
            "`name` VARCHAR(200),`household` VARCHAR(100),"
            "`device_name` VARCHAR(100),"
            "`event_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

        ok = ok && execSQL(
            "CREATE TABLE IF NOT EXISTS `Lebensmittel_Scanner`.`removed_items` ("
            "`label_barcode` VARCHAR(60) NOT NULL,"
            "`household` VARCHAR(100),"
            "`removed_by` VARCHAR(100),"
            "`removed_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (`label_barcode`,`household`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

        ok = ok && execSQL(
            "CREATE TABLE IF NOT EXISTS `Lebensmittel_Scanner`.`product_cache` ("
            "`barcode` VARCHAR(60) NOT NULL PRIMARY KEY,"
            "`name` VARCHAR(200),`brand` VARCHAR(100),`quantity` VARCHAR(60),"
            "`category` VARCHAR(100),`nutriscore` VARCHAR(10),`labels` TEXT,"
            "`updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

        // Use IDENTIFIED BY (MariaDB / MySQL ≤5.7 syntax; our MySQLDirect client
        // only supports mysql_native_password which is the default on MariaDB).
        // CREATE USER IF NOT EXISTS skips if the user already exists;
        // ALTER USER then updates/confirms the password in both cases.
        ok = ok && execSQL(
            String("CREATE USER IF NOT EXISTS 'lebensmittel_sync'@'%' IDENTIFIED BY '")
            + syncPass + "'");

        ok = ok && execSQL(
            String("ALTER USER 'lebensmittel_sync'@'%' IDENTIFIED BY '") + syncPass + "'");

        ok = ok && execSQL(
            "GRANT SELECT,INSERT,UPDATE,DELETE ON `Lebensmittel_Scanner`.* "
            "TO 'lebensmittel_sync'@'%'");

        ok = ok && execSQL("FLUSH PRIVILEGES");

        db.close();
        Serial.printf("[SetupWizard] All SQL steps ok=%d\n", (int)ok);
        Serial.flush();

        if (ok) {
            // Save sync credentials (root credentials are NOT stored)
            JsonDocument cfg;
            cfg["ip"]   = host;
            cfg["user"] = "lebensmittel_sync";
            cfg["pass"] = syncPass;
            saveJson("/server_sync_config.json", cfg);
            sync_manager.loadConfig();
            Serial.println("[SetupWizard] Config saved OK");
            Serial.flush();
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            String err = String("{\"ok\":false,\"error\":\"MySQL-Setup fehlgeschlagen: ")
                         + lastErr + "\"}";
            req->send(200, "application/json", err);
        }

    }, nullptr, bodyCollect);

    _server.on("/api/server-sync", HTTP_GET, [](AsyncWebServerRequest *req) {
        Serial.println("[Sync] GET /api/server-sync");
        Serial.flush();
        JsonDocument doc;
        loadJson("/server_sync_config.json", doc, "{}");
        doc["connected"] = sync_manager.wasLastSyncOk();
        doc["lastSync"]  = (long)sync_manager.getLastSync();
        doc["pending"]   = (int)sync_manager.pending();
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    _server.on("/api/server-sync",   HTTP_POST, [](AsyncWebServerRequest *req) {
        mergePost(req, "/server_sync_config.json", "{}");
        // Reload config so the next loop() picks up the new ip/user/pass
        sync_manager.loadConfig();
    }, nullptr, bodyCollect);

    // ---- WIFI ----
    // All /api/wifi/* sub-routes registered BEFORE /api/wifi GET+POST (prefix matching)
    _server.on("/api/wifi/ap", HTTP_POST, cfgPost("/wifi_config.json"), nullptr, bodyCollect);

    // WiFi scan — start. Run synchronously and cache a frontend-ready result.
    // This avoids races between AsyncWebServer callbacks and WiFi's global scan buffer.
    _server.on("/api/wifi/scan-start", HTTP_GET, [](AsyncWebServerRequest *req) {
        Serial.println("[WiFiScan] /api/wifi/scan-start called");
        Serial.flush();

        if (_scanRunning) {
            sendScanResult(req);
            return;
        }

        _scanRunning = true;
        resetScanCache();

        int result = wifi_manager.scanNetworks(true);
        cacheScanResults(result, "api");
        WiFi.scanDelete();

        _scanRunning = false;
        Serial.printf("[WiFiScan] Cached %d visible network(s), raw result=%d\n", _scanCount, _scanResult);
        Serial.flush();
        sendScanResult(req);
    });

    // WiFi scan — poll cached result. Keep the cache stable across repeated polls.
    _server.on("/api/wifi/scan-result", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendScanResult(req);
    });

    // WiFi connect — save credentials and start connection
    _server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            handleWiFiConnectRequest(req, "/api/wifi/connect");
        },
        nullptr, bodyCollect);

    // WiFi status — used by setup page to poll connection result
    _server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        wl_status_t st = WiFi.status();
        Serial.printf("[API] GET /api/wifi/status host=%s status=%d connected=%d\n",
                      req->host().c_str(), static_cast<int>(st), st == WL_CONNECTED);
        Serial.flush();
        bool connected = (st == WL_CONNECTED);
        bool failed    = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
        JsonDocument doc;
        doc["connected"] = connected;
        doc["failed"]    = failed;
        doc["status"]    = static_cast<int>(st);
        doc["ip"]        = connected ? WiFi.localIP().toString() : "";
        doc["ssid"]      = connected ? WiFi.SSID() : "";
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

    // Parent /api/wifi routes (after all sub-routes)
    _server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
        Serial.printf("[API] GET /api/wifi host=%s connected=%d\n", req->host().c_str(), WiFi.status() == WL_CONNECTED);
        Serial.flush();
        JsonDocument doc;
        loadJson("/wifi_config.json", doc, "{}");
        doc["connected"] = (WiFi.status() == WL_CONNECTED);
        doc["ssid"]      = wifi_manager.getSavedSSID();
        doc["ip"]        = WiFi.localIP().toString();
        doc["rssi"]      = WiFi.RSSI();
        appendScanCache(doc);
        Serial.printf("[API] /api/wifi includes scan cache ready=%d count=%d source=%s\n",
                      _scanReady, _scanCount, _scanSource.c_str());
        Serial.flush();
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    _server.on("/api/wifi", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument probe;
            if (deserializeJson(probe, _body) == DeserializationError::Ok
                && !probe["ssid"].isNull()) {
                handleWiFiConnectRequest(req, "/api/wifi");
                return;
            }
            Serial.printf("[API] POST /api/wifi config bodyLen=%u\n", static_cast<unsigned>(_body.length()));
            Serial.flush();
            mergePost(req, "/wifi_config.json", "{}");
        },
        nullptr, bodyCollect);

    // ---- SIMPLE STUBS ----
    auto stub = [](const char *msg) {
        return [msg](AsyncWebServerRequest *req) {
            req->send(200, "application/json", msg);
        };
    };
    _server.on("/api/test-print", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            if (!_printer) {
                Logger::error("Printer", "Web test print requested but printer manager is missing");
                req->send(500, "application/json", "{\"ok\":false,\"error\":\"printer not initialized\"}");
                return;
            }

            JsonDocument inp;
            deserializeJson(inp, _body);
            String type = inp["type"] | "text";
            Logger::info("Printer", String("Web test print request type=") + type + " ready=" + (_printer->isReady() ? 1 : 0) + " baud=" + _printer->baud());

            size_t bytes = 0;
            String mode = type;
            if (type == "qr") {
                bytes = _printer->printTestPage(true);
            } else if (type == "plain") {
                bytes = _printer->printPlainTest();
            } else if (type == "baud") {
                bytes = _printer->printBaudProbe();
            } else {
                mode = "label";
                InventoryItem testItem;
                testItem.name       = "Testprodukt";
                testItem.barcode    = "4000417025005";
                testItem.addedDate  = "14.05.2026";
                testItem.expiryDate = "31.12.2026";
                testItem.quantity   = 1;
                bytes = _printer->printLabel(testItem) ? 1 : 0;
            }

            bool printed = bytes > 0;
            JsonDocument doc;
            doc["ok"] = printed;
            doc["ready"] = _printer->isReady();
            doc["baudrate"] = _printer->baud();
            doc["mode"] = mode;
            doc["bytes"] = static_cast<uint32_t>(bytes);
            doc["message"] = printed ? "Testdruck gesendet" : "Drucker nicht bereit";
            String body;
            serializeJson(doc, body);
            Logger::info("Printer", String("Web test print response ok=") + (printed ? 1 : 0) + " bytes=" + static_cast<uint32_t>(bytes));
            req->send(printed ? 200 : 503, "application/json", body);
        },
        nullptr, bodyCollect);
    _server.on("/api/audio/volume", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            JsonDocument inp;
            deserializeJson(inp, _body);
            uint8_t vol = (uint8_t)constrain((int)(inp["volume"] | 70), 0, 100);
            audio_obj.setVolume(vol);
            String body = String("{\"ok\":true,\"volume\":") + vol + "}";
            req->send(200, "application/json", body);
        }, nullptr, bodyCollect);

    _server.on("/api/audio-test", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            JsonDocument inp;
            deserializeJson(inp, _body);
            uint16_t freq = inp["freq"] | 1000;
            uint16_t dur  = inp["dur"]  | 300;
            freq = constrain(freq, 50, 8000);
            dur  = constrain(dur,  50, 5000);
            audio_obj.playTone(freq, dur);
            Logger::info("Audio", String("Web tone test: ") + freq + " Hz, " + dur + " ms");
            String body = String("{\"ok\":true,\"freq\":") + freq + ",\"dur\":" + dur + "}";
            req->send(200, "application/json", body);
        }, nullptr, bodyCollect);

    _server.on("/api/print-manual", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            if (!_printer || !_printer->isReady()) {
                req->send(503, "application/json", "{\"ok\":false,\"message\":\"Drucker nicht bereit\"}");
                return;
            }
            JsonDocument inp;
            deserializeJson(inp, _body);
            String text  = inp["text"]  | "";
            String align = inp["align"] | "left";
            if (text.isEmpty()) {
                req->send(400, "application/json", "{\"ok\":false,\"message\":\"Kein Text\"}");
                return;
            }
            // Print via EscPosPrinter-compatible API through printer manager
            _printer->printManualLabel(text, align == "center");
            req->send(200, "application/json", "{\"ok\":true,\"message\":\"Gedruckt\"}");
        }, nullptr, bodyCollect);

    _server.on("/api/buzzer-test",          HTTP_POST, stub("{\"ok\":true}"));
    _server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *req) {
        String json;
        Logger::getLogJson(json);
        req->send(200, "application/json", json);
    });
    _server.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest *req) {
        Logger::clearLog();
        req->send(200, "application/json", "{\"ok\":true}");
    });
    _server.on("/api/scanner/ble-scan", HTTP_GET, [](AsyncWebServerRequest *req) {
        portENTER_CRITICAL(&_bleScanMux);
        int state = _bleScanState;
        portEXIT_CRITICAL(&_bleScanMux);

        if (state == 2) {
            // Results ready — return and reset to idle
            portENTER_CRITICAL(&_bleScanMux);
            String json = _bleScanJson;
            _bleScanState = 0;
            portEXIT_CRITICAL(&_bleScanMux);
            req->send(200, "application/json", json);
            return;
        }
        if (state == 1) {
            req->send(202, "application/json", "{\"scanning\":true}");
            return;
        }
        // state == 0: start a new scan
        portENTER_CRITICAL(&_bleScanMux);
        _bleScanState = 1;
        portEXIT_CRITICAL(&_bleScanMux);
        xTaskCreate([](void *) {
            auto devices = ble_scanner.scanDevices(5);
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (const auto &dev : devices) {
                JsonObject o = arr.add<JsonObject>();
                o["address"] = dev.address;
                o["name"]    = dev.name;
                o["rssi"]    = dev.rssi;
                o["hid"]     = dev.hid;
            }
            portENTER_CRITICAL(&_bleScanMux);
            serializeJson(doc, _bleScanJson);
            _bleScanState = 2;
            portEXIT_CRITICAL(&_bleScanMux);
            vTaskDelete(nullptr);
        }, "bleScan", 8192, nullptr, 1, nullptr);
        req->send(202, "application/json", "{\"scanning\":true}");
    });
    _server.on("/api/scanner/ble-connect", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument inp;
            if (deserializeJson(inp, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
                return;
            }
            String address = inp["address"] | "";
            String name = inp["name"] | "";
            if (address.isEmpty()) {
                req->send(400, "application/json", "{\"error\":\"address required\"}");
                return;
            }
            ble_scanner.requestConnect(address, name);
            JsonDocument doc;
            doc["ok"] = true;
            doc["queued"] = true;
            doc["status"] = ble_scanner.getStatus();
            doc["device"] = name;
            doc["address"] = address;
            String body;
            serializeJson(doc, body);
            req->send(202, "application/json", body);
        },
        nullptr, bodyCollect);
    _server.on("/api/scanner/ble-disconnect", HTTP_POST, [](AsyncWebServerRequest *req) {
        ble_scanner.setAutoReconnect(false);
        ble_scanner.disconnect();
        req->send(200, "application/json", "{\"ok\":true}");
    });
    _server.on("/api/ota-url", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            if (!_otaUrlDone) {
                req->send(409, "application/json", "{\"ok\":false,\"error\":\"OTA laeuft bereits\"}");
                return;
            }
            JsonDocument doc;
            if (deserializeJson(doc, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
                return;
            }
            String url = doc["url"] | "";
            if (url.isEmpty()) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"url missing\"}"); return; }
            char *urlBuf = strdup(url.c_str());
            xTaskCreate(otaUrlTask, "ota_url", 32768, urlBuf, 3, nullptr);
            req->send(202, "application/json", "{\"ok\":true}");
        }, nullptr, bodyCollect);

    _server.on("/api/ota-progress", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["pct"]  = (int)_otaUrlPct;
        doc["done"] = (bool)_otaUrlDone;
        doc["ok"]   = (bool)_otaUrlOk;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/ota-url-fs", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            if (!_otaFsDone) {
                req->send(409, "application/json", "{\"ok\":false,\"error\":\"FS-OTA laeuft bereits\"}");
                return;
            }
            JsonDocument doc;
            if (deserializeJson(doc, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
                return;
            }
            String url = doc["url"] | "";
            if (url.isEmpty()) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"url missing\"}"); return; }
            char *urlBuf = strdup(url.c_str());
            xTaskCreate(otaFsUrlTask, "ota_fs_url", 32768, urlBuf, 3, nullptr);
            req->send(202, "application/json", "{\"ok\":true}");
        }, nullptr, bodyCollect);

    _server.on("/api/ota-fs-progress", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["pct"]  = (int)_otaFsPct;
        doc["done"] = (bool)_otaFsDone;
        doc["ok"]   = (bool)_otaFsOk;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // ---- STATS ----
    _server.on("/api/stats", HTTP_GET, [this](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["totalStored"]    = _invMgr ? (int)_invMgr->items().size() : 0;
        doc["totalConsumed"]  = 0;
        doc["totalWasted"]    = 0;
        doc["avgStorageDays"] = 0;
        doc["topProducts"].to<JsonArray>();
        doc["categoryUsage"].to<JsonArray>();
        doc["history"].to<JsonArray>();
        sendJson(req, doc);
    });

    // ---- SCAN LOG ----
    _server.on("/api/scan-log", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/scan_log.json", "[]");
    });
    _server.on("/api/scan-log/clear", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument doc;
            doc.to<JsonArray>();
            saveJson("/scan_log.json", doc);
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // ---- SYSTEM ACTIONS ----
    _server.on("/api/restart", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":true}");
            delay(300);
            esp_restart();
        });
    _server.on("/api/factory-reset", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":true}");
            delay(300);
            AppFS::formatUserData();
            esp_restart();
        });
    _server.on("/api/format-fs", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            AppFS::formatUserData();
            req->send(200, "application/json", "{\"ok\":true}");
        });
    _server.on("/api/cache/clear", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            AppFS::fs().remove("/off_cache.json");
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // ---- PRODUCT TEMPLATES ----
    // GET /api/templates  → return all templates
    _server.on("/api/templates", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/templates.json", "[]");
    });

    // POST /api/templates/add  → add one template {name, category, shelfDays}
    _server.on("/api/templates/add", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, inp;
            loadJson("/templates.json", arr, "[]");
            if (deserializeJson(inp, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
                return;
            }
            String name = inp["name"] | "";
            if (name.isEmpty()) {
                req->send(400, "application/json", "{\"error\":\"name required\"}");
                return;
            }
            JsonObject item = arr.as<JsonArray>().add<JsonObject>();
            item["id"]        = String(millis());
            item["name"]      = name;
            item["category"]  = inp["category"] | "Allgemein";
            item["shelfDays"] = inp["shelfDays"] | 14;
            saveJson("/templates.json", arr);
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // POST /api/templates/delete  → delete template by id {id}
    _server.on("/api/templates/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, inp;
            loadJson("/templates.json", arr, "[]");
            if (deserializeJson(inp, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
                return;
            }
            String del_id = inp["id"] | "";
            JsonDocument out;
            JsonArray out_arr = out.to<JsonArray>();
            for (JsonObject obj : arr.as<JsonArray>()) {
                String oid = obj["id"] | "";
                if (oid != del_id) out_arr.add(obj);
            }
            saveJson("/templates.json", out);
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- OTA FILE UPLOAD ----
    _server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            bool ok = !Update.hasError();
            req->send(200, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Update failed\"}");
            if (ok) { delay(200); WiFi.disconnect(true); WiFi.mode(WIFI_OFF); delay(300); esp_restart(); }
        },
        [](AsyncWebServerRequest *req, const String &filename,
           size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Logger::info("OTA", "Start: " + filename);
                Update.begin(UPDATE_SIZE_UNKNOWN);
            }
            if (Update.isRunning()) Update.write(data, len);
            if (final && !Update.end(true))
                Logger::error("OTA", "end() failed");
        });
}
