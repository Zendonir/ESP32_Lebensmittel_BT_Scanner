#include "WebInterface.h"
#include "../core/Logger.h"
#include "../storage/JsonStorage.h"
#include "../inventory/InventoryManager.h"
#include "../models/InventoryItem.h"
#include "wifi_manager.h"
#include "../scanner/BarcodeManager.h"
#include "../printer/PrinterManager.h"
#include "config.h"

#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_system.h>

/* ── WiFi scan cache ──────────────────────────────────────────── */
static constexpr int WIFI_SCAN_CACHE_MAX = 30;

struct WiFiScanEntry {
    String ssid;
    int32_t rssi = 0;
    int32_t channel = 0;
    bool open = false;
};

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
            bool    backfeed      = printerConfig["backfeed"]      | false;
            uint8_t backfeedLines = (uint8_t)(printerConfig["backfeedLines"] | 3);
            _printer->setBackfeedConfig(backfeed, backfeedLines);
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

void WebInterface::registerStaticRoutes() {
    // Root: show WiFi setup while offline/AP-only; /?app=1 opens the main SPA explicitly
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        bool forceApp = req->hasParam("app");
        bool needsSetup = !wifi_manager.isConnected();
        Serial.printf("[Web] GET / host=%s forceApp=%d needsSetup=%d\n",
                      req->host().c_str(), forceApp, needsSetup);
        Serial.flush();
        if (!forceApp && needsSetup && LittleFS.exists("/wifi-setup.html")) {
            sendNoCacheFile(req, "/wifi-setup.html", "text/html");
            return;
        }
        if (LittleFS.exists("/index.html")) {
            sendNoCacheFile(req, "/index.html", "text/html");
            return;
        }
        // Filesystem not uploaded yet
        req->send(200, "text/html", FPSTR(SETUP_FALLBACK));
    });

    // Explicit WiFi setup page (always accessible for re-setup)
    _server.on("/wifi-setup", HTTP_GET, [](AsyncWebServerRequest *req) {
        Serial.printf("[Web] GET /wifi-setup host=%s\n", req->host().c_str());
        Serial.flush();
        if (LittleFS.exists("/wifi-setup.html"))
            sendNoCacheFile(req, "/wifi-setup.html", "text/html");
        else
            req->send(404, "text/plain", "wifi-setup.html not found – run uploadfs");
    });
    _server.on("/wifi-setup.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        Serial.printf("[Web] GET /wifi-setup.html host=%s\n", req->host().c_str());
        Serial.flush();
        if (LittleFS.exists("/wifi-setup.html"))
            sendNoCacheFile(req, "/wifi-setup.html", "text/html");
        else
            req->send(404, "text/plain", "wifi-setup.html not found – run uploadfs");
    });

    // Captive portal detection endpoints — redirect to WiFi setup.
    // Different OSes probe different URLs before showing their captive portal UI.
    auto captive = [](AsyncWebServerRequest *req) {
        String url = "http://";
        url += WiFi.softAPIP().toString();
        url += "/wifi-setup";
        Serial.printf("[Captive] %s host=%s -> %s\n",
                      req->url().c_str(), req->host().c_str(), url.c_str());
        Serial.flush();
        req->redirect(url);
    };
    _server.on("/generate_204",                  HTTP_GET, captive); // Android
    _server.on("/gen_204",                       HTTP_GET, captive); // Android variant
    _server.on("/hotspot-detect.html",           HTTP_GET, captive); // Apple
    _server.on("/library/test/success.html",     HTTP_GET, captive); // Apple newer
    _server.on("/ncsi.txt",                      HTTP_GET, captive); // Windows
    _server.on("/connecttest.txt",               HTTP_GET, captive); // Windows
    _server.on("/redirect",                      HTTP_GET, captive); // Microsoft Edge
    _server.on("/fwlink",                        HTTP_GET, captive); // Windows legacy
    _server.on("/canonical.html",                HTTP_GET, captive); // Ubuntu/Firefox
    _server.on("/success.txt",                   HTTP_GET, captive); // Firefox
    _server.on("/kindle-wifi/wifistub.html",     HTTP_GET, captive); // Kindle

    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/style.css"))
            sendNoCacheFile(req, "/style.css", "text/css");
        else
            req->send(404, "text/plain", "style.css not found – run uploadfs");
    });

    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/app.js"))
            sendNoCacheFile(req, "/app.js", "application/javascript");
        else
            req->send(404, "text/plain", "app.js not found – run uploadfs");
    });

    _server.onNotFound([](AsyncWebServerRequest *req) {
        const String &path = req->url();
        Serial.printf("[Web] 404/onNotFound %s host=%s\n", path.c_str(), req->host().c_str());
        Serial.flush();
        if (path.startsWith("/api/")) {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
            return;
        }
        if (!wifi_manager.isConnected() && LittleFS.exists("/wifi-setup.html")) {
            sendNoCacheFile(req, "/wifi-setup.html", "text/html");
            return;
        }
        // SPA fallback — all non-API routes load the app
        if (LittleFS.exists("/index.html"))
            sendNoCacheFile(req, "/index.html", "text/html");
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

static void bodyCollect(AsyncWebServerRequest *, uint8_t *data, size_t len,
                        size_t index, size_t total) {
    if (index == 0) {
        _body = "";
        _body.reserve(total);
    }
    _body += String(reinterpret_cast<char *>(data), len);
}

static bool loadJson(const char *path, JsonDocument &doc,
                     const char *fallback = "{}") {
    File f = LittleFS.open(path, "r");
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
    File f = LittleFS.open(tmp.c_str(), "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    LittleFS.remove(path);
    return LittleFS.rename(tmp.c_str(), path);
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
        doc["firmware"]   = "1.0.0";
        doc["heap"]       = ESP.getFreeHeap();
        doc["heapTotal"]  = ESP.getHeapSize();
        doc["psram"]      = ESP.getFreePsram();
        doc["psramTotal"] = ESP.getPsramSize();
        doc["cpuFreq"]    = ESP.getCpuFreqMHz();
        doc["mac"]        = WiFi.macAddress();
        doc["ip"]         = WiFi.localIP().toString();
        const char *hn    = WiFi.getHostname();
        doc["hostname"]   = hn ? hn : "esp32-scanner";
        doc["fsUsed"]     = (uint32_t)LittleFS.usedBytes();
        doc["fsTotal"]    = (uint32_t)LittleFS.totalBytes();
        unsigned long s   = millis() / 1000;
        char up[32];
        snprintf(up, sizeof(up), "%lud %02luh %02lum",
                 s / 86400, (s % 86400) / 3600, (s % 3600) / 60);
        doc["uptime"] = up;
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

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
            }
        }
        sendJson(req, doc);
    });

    // ---- INVENTORY (POST – add) ----
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
            _invMgr->addItem(item);
            sendOk(req);
        },
        nullptr, bodyCollect);

    // ---- INVENTORY DELETE ----
    _server.on("/api/inventory/delete", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            if (!_invMgr) { sendError(req, "Inventory nicht verfügbar"); return; }
            JsonDocument key;
            if (deserializeJson(key, _body) != DeserializationError::Ok) {
                sendError(req, "Ungültiges JSON", 400); return;
            }
            String lb = key["labelBarcode"] | "";
            String bc = key["barcode"]      | "";
            if (!lb.isEmpty())
                _invMgr->removeByLabel(lb);
            else if (!bc.isEmpty())
                _invMgr->removeByBarcode(bc);
            sendOk(req);
        },
        nullptr, bodyCollect);

    // ---- CUSTOM PRODUCTS ----
    _server.on("/api/custom-products", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/custom_products.json", "[]");
    });
    _server.on("/api/custom-products", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/custom_products.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            if (deserializeJson(item, _body) == DeserializationError::Ok)
                arr.as<JsonArray>().add(item.as<JsonObject>());
            saveJson("/custom_products.json", arr);
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
                String bc   = key["barcode"] | "";
                String name = key["name"]    | "";
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if ((String)(v["barcode"] | "") == bc) continue;
                    if ((String)(v["name"]    | "") == name) continue;
                    out.as<JsonArray>().add(v);
                }
                saveJson("/custom_products.json", out);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- CATEGORIES ----
    _server.on("/api/categories", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/categories.json", "[]");
    });
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

    // ---- SHOPPING LIST ----
    _server.on("/api/shopping-list", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/shopping_list.json", "[]");
    });
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

    // ---- CONFIG FILES (simple GET/POST) ----
    auto cfgGet = [](const char *file) {
        return [file](AsyncWebServerRequest *req) { sendFile(req, file, "{}"); };
    };
    auto cfgPost = [](const char *file) {
        return [file](AsyncWebServerRequest *req) { mergePost(req, file, "{}"); };
    };

    _server.on("/api/ui-config",     HTTP_GET,  cfgGet("/ui_config.json"));
    _server.on("/api/ui-config",     HTTP_POST, cfgPost("/ui_config.json"), nullptr, bodyCollect);
    _server.on("/api/ui-config/reset", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            LittleFS.remove("/ui_config.json");
            req->send(200, "application/json", "{\"ok\":true}");
        });

    _server.on("/api/font-config",   HTTP_GET,  cfgGet("/font_config.json"));
    _server.on("/api/font-config",   HTTP_POST, cfgPost("/font_config.json"), nullptr, bodyCollect);

    _server.on("/api/printer-config", HTTP_GET, [this](AsyncWebServerRequest *req) {
        JsonDocument doc;
        loadJson("/printer_config.json", doc, "{}");
        if (!doc["baudrate"].is<uint32_t>())    doc["baudrate"]      = _printer ? _printer->baud() : UART_BAUD;
        if (!doc["labelLen"].is<int>())          doc["labelLen"]      = 40;
        if (!doc["backfeedLines"].is<int>())     doc["backfeedLines"] = 3;
        if (!doc["backfeed"].is<bool>())         doc["backfeed"]      = false;
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
            uint32_t baud         = incoming["baudrate"]      | UART_BAUD;
            bool     backfeed     = incoming["backfeed"]      | false;
            uint8_t  backfeedLines= (uint8_t)(incoming["backfeedLines"] | 3);
            Logger::info("Printer", String("Web config update baud=") + baud
                + " backfeed=" + backfeed + " lines=" + backfeedLines);
            mergePost(req, "/printer_config.json", "{}");
            if (_printer) {
                _printer->configure(baud);
                _printer->setBackfeedConfig(backfeed, backfeedLines);
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

                JsonDocument existing;
                loadJson("/scanner_config.json", existing, "{}");
                existing["mode"] = "ble_hid";
                existing["autoReconnect"] = ble_scanner.getAutoReconnect();
                existing["bleAddress"] = ble_scanner.getDeviceAddress();
                existing["bleDevice"] = ble_scanner.getDeviceName();
                saveJson("/scanner_config.json", existing);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    _server.on("/api/mqtt",          HTTP_GET,  cfgGet("/mqtt_config.json"));
    _server.on("/api/mqtt",          HTTP_POST, cfgPost("/mqtt_config.json"), nullptr, bodyCollect);

    _server.on("/api/telegram",      HTTP_GET,  cfgGet("/telegram_config.json"));
    _server.on("/api/telegram",      HTTP_POST, cfgPost("/telegram_config.json"), nullptr, bodyCollect);

    _server.on("/api/server-sync",   HTTP_GET,  cfgGet("/server_sync_config.json"));
    _server.on("/api/server-sync",   HTTP_POST, cfgPost("/server_sync_config.json"), nullptr, bodyCollect);

    // ---- WIFI ----
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

    // ---- SIMPLE STUBS ----
    auto stub = [](const char *msg) {
        return [msg](AsyncWebServerRequest *req) {
            req->send(200, "application/json", msg);
        };
    };
    _server.on("/api/mqtt/test",            HTTP_POST, stub("{\"ok\":false,\"message\":\"MQTT nicht konfiguriert\"}"));
    _server.on("/api/telegram/test",        HTTP_POST, stub("{\"ok\":false,\"message\":\"Telegram nicht konfiguriert\"}"));
    _server.on("/api/server-sync/test",     HTTP_POST, stub("{\"ok\":false,\"message\":\"Sync nicht konfiguriert\"}"));
    _server.on("/api/server-sync/queue",    HTTP_GET,  stub("[]"));
    _server.on("/api/server-sync/queue/clear", HTTP_POST, stub("{\"ok\":true}"));
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
    _server.on("/api/test-backfeed", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            if (!_printer) {
                req->send(503, "application/json", "{\"ok\":false,\"error\":\"printer not initialized\"}");
                return;
            }
            JsonDocument inp;
            deserializeJson(inp, _body);
            // cmd: "ESC_j"=0x6A, "ESC_K"=0x4B, "ESC_e"=0x65, or raw hex string "0x??"
            String cmdStr     = inp["cmd"]     | "ESC_K";
            uint16_t dots     = inp["dots"]    | 72;
            uint8_t  chunk    = (uint8_t)(inp["chunk"]   | 0);
            uint16_t delayMs  = inp["delay"]   | 50;
            bool     doFlush  = inp["flush"]   | true;

            uint8_t cmdByte = 0x4B; // default ESC K
            if      (cmdStr == "ESC_j") cmdByte = 0x6A;
            else if (cmdStr == "ESC_K") cmdByte = 0x4B;
            else if (cmdStr == "ESC_e") cmdByte = 0x65;
            else if (cmdStr.startsWith("0x") || cmdStr.startsWith("0X"))
                cmdByte = (uint8_t)strtol(cmdStr.c_str(), nullptr, 16);

            Logger::info("Printer", String("Backfeed test: ESC 0x") + String(cmdByte, HEX)
                + " dots=" + dots + " chunk=" + chunk + " delay=" + delayMs + " flush=" + doFlush);

            bool ok = _printer->testBackfeed(cmdByte, dots, chunk, delayMs, doFlush);

            JsonDocument doc;
            doc["ok"]      = ok;
            doc["cmd"]     = cmdStr;
            doc["cmdByte"] = "0x" + String(cmdByte, HEX);
            doc["dots"]    = dots;
            doc["chunk"]   = chunk;
            doc["delay"]   = delayMs;
            doc["flush"]   = doFlush;
            doc["message"] = ok ? "Rücklauf gesendet" : "Drucker nicht bereit";
            String body;
            serializeJson(doc, body);
            req->send(ok ? 200 : 503, "application/json", body);
        },
        nullptr, bodyCollect);

    _server.on("/api/buzzer-test",          HTTP_POST, stub("{\"ok\":true}"));
    _server.on("/api/logs",                 HTTP_GET,  stub("[]"));
    _server.on("/api/logs/clear",           HTTP_POST, stub("{\"ok\":true}"));
    _server.on("/api/scanner/ble-scan", HTTP_GET, [](AsyncWebServerRequest *req) {
        auto devices = ble_scanner.scanDevices(5);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto &dev : devices) {
            JsonObject o = arr.add<JsonObject>();
            o["address"] = dev.address;
            o["name"] = dev.name;
            o["rssi"] = dev.rssi;
            o["hid"] = dev.hid;
        }
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
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
    _server.on("/api/ota-url",              HTTP_POST, stub("{\"ok\":true,\"message\":\"OTA gestartet\"}"), nullptr, bodyCollect);

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
            LittleFS.format();
            esp_restart();
        });
    _server.on("/api/format-fs", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            LittleFS.format();
            req->send(200, "application/json", "{\"ok\":true}");
        });
    _server.on("/api/cache/clear", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            LittleFS.remove("/off_cache.json");
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // ---- OTA FILE UPLOAD ----
    _server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            bool ok = !Update.hasError();
            req->send(200, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Update failed\"}");
            if (ok) { delay(500); esp_restart(); }
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
