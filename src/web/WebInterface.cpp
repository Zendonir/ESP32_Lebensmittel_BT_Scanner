#include "WebInterface.h"
#include "../core/Logger.h"
#include "../storage/JsonStorage.h"
#include "wifi_manager.h"

#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* ── Background WiFi scan state ─────────────────────────────── */
static volatile bool _scanRunning = false;
static volatile bool _scanReady   = false;
static volatile int  _scanCount   = 0;

static void wifiScanTask(void *) {
    // Use Serial.printf directly — guaranteed output even if Logger has issues
    Serial.println("[WiFiScan] Task started");
    Serial.flush();

    WiFi.scanDelete();

    Serial.println("[WiFiScan] Calling WiFi.scanNetworks()...");
    Serial.flush();

    int n = WiFi.scanNetworks(); // simplest blocking form, all defaults

    Serial.printf("[WiFiScan] Result: %d (WIFI_SCAN_FAILED=-2)\n", n);
    Serial.flush();

    for (int i = 0; i < n && i < 5; i++) {
        Serial.printf("[WiFiScan]   #%d  %s  %d dBm\n",
                      i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
    Serial.flush();

    _scanCount   = (n > 0) ? n : 0;
    _scanReady   = true;
    _scanRunning = false;
    vTaskDelete(nullptr);
}

WebInterface::WebInterface(uint16_t port) : _server(port) {}

void WebInterface::begin() {
    registerStaticRoutes();
    registerApiRoutes();
    _server.begin();
    Logger::info("Web", "Web interface started on port 80");
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

void WebInterface::registerStaticRoutes() {
    // Root: show WiFi setup if no credentials saved, otherwise the main SPA
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        bool hasCreds = wifi_manager.hasCredentials();
        if (!hasCreds && LittleFS.exists("/wifi-setup.html")) {
            req->send(LittleFS, "/wifi-setup.html", "text/html");
            return;
        }
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
            return;
        }
        // Filesystem not uploaded yet
        req->send(200, "text/html", FPSTR(SETUP_FALLBACK));
    });

    // Explicit WiFi setup page (always accessible for re-setup)
    _server.on("/wifi-setup", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/wifi-setup.html"))
            req->send(LittleFS, "/wifi-setup.html", "text/html");
        else
            req->send(404, "text/plain", "wifi-setup.html not found – run uploadfs");
    });
    _server.on("/wifi-setup.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/wifi-setup.html"))
            req->send(LittleFS, "/wifi-setup.html", "text/html");
        else
            req->send(404, "text/plain", "wifi-setup.html not found – run uploadfs");
    });

    // Captive portal detection endpoints — redirect to WiFi setup
    auto captive = [](AsyncWebServerRequest *req) {
        String url = "http://";
        url += WiFi.softAPIP().toString();
        url += "/wifi-setup";
        req->redirect(url);
    };
    _server.on("/generate_204",          HTTP_GET, captive);
    _server.on("/hotspot-detect.html",   HTTP_GET, captive);
    _server.on("/ncsi.txt",              HTTP_GET, captive);
    _server.on("/connecttest.txt",       HTTP_GET, captive);
    _server.on("/canonical.html",        HTTP_GET, captive);
    _server.on("/success.txt",           HTTP_GET, captive);

    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/style.css"))
            req->send(LittleFS, "/style.css", "text/css");
        else
            req->send(404, "text/plain", "style.css not found – run uploadfs");
    });

    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/app.js"))
            req->send(LittleFS, "/app.js", "application/javascript");
        else
            req->send(404, "text/plain", "app.js not found – run uploadfs");
    });

    _server.onNotFound([](AsyncWebServerRequest *req) {
        const String &path = req->url();
        if (path.startsWith("/api/")) {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
            return;
        }
        // SPA fallback — all non-API routes load the app
        if (LittleFS.exists("/index.html"))
            req->send(LittleFS, "/index.html", "text/html");
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
    _server.on("/api/inventory", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendFile(req, "/inventory.json", "[]");
    });

    // ---- INVENTORY (POST – add) ----
    _server.on("/api/inventory", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, item;
            loadJson("/inventory.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            if (deserializeJson(item, _body) == DeserializationError::Ok)
                arr.as<JsonArray>().add(item.as<JsonObject>());
            saveJson("/inventory.json", arr);
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // ---- INVENTORY DELETE ----
    _server.on("/api/inventory/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr, key, out;
            out.to<JsonArray>();
            loadJson("/inventory.json", arr, "[]");
            if (deserializeJson(key, _body) == DeserializationError::Ok
                && arr.is<JsonArray>()) {
                String lb = key["labelBarcode"] | "";
                String bc = key["barcode"]      | "";
                for (JsonVariant v : arr.as<JsonArray>()) {
                    String vlb = v["labelBarcode"] | "";
                    String vbc = v["barcode"]      | "";
                    if (vlb == lb || vbc == bc) continue;
                    out.as<JsonArray>().add(v);
                }
                saveJson("/inventory.json", out);
            }
            req->send(200, "application/json", "{\"ok\":true}");
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

    _server.on("/api/printer-config",HTTP_GET,  cfgGet("/printer_config.json"));
    _server.on("/api/printer-config",HTTP_POST, cfgPost("/printer_config.json"), nullptr, bodyCollect);

    _server.on("/api/scanner-config",HTTP_GET,  cfgGet("/scanner_config.json"));
    _server.on("/api/scanner-config",HTTP_POST, cfgPost("/scanner_config.json"), nullptr, bodyCollect);

    _server.on("/api/mqtt",          HTTP_GET,  cfgGet("/mqtt_config.json"));
    _server.on("/api/mqtt",          HTTP_POST, cfgPost("/mqtt_config.json"), nullptr, bodyCollect);

    _server.on("/api/telegram",      HTTP_GET,  cfgGet("/telegram_config.json"));
    _server.on("/api/telegram",      HTTP_POST, cfgPost("/telegram_config.json"), nullptr, bodyCollect);

    _server.on("/api/server-sync",   HTTP_GET,  cfgGet("/server_sync_config.json"));
    _server.on("/api/server-sync",   HTTP_POST, cfgPost("/server_sync_config.json"), nullptr, bodyCollect);

    // ---- WIFI ----
    _server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        loadJson("/wifi_config.json", doc, "{}");
        doc["connected"] = (WiFi.status() == WL_CONNECTED);
        doc["ssid"]      = wifi_manager.getSavedSSID();
        doc["ip"]        = WiFi.localIP().toString();
        doc["rssi"]      = WiFi.RSSI();
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    _server.on("/api/wifi",    HTTP_POST, cfgPost("/wifi_config.json"), nullptr, bodyCollect);
    _server.on("/api/wifi/ap", HTTP_POST, cfgPost("/wifi_config.json"), nullptr, bodyCollect);

    // WiFi scan — start
    _server.on("/api/wifi/scan-start", HTTP_GET, [](AsyncWebServerRequest *req) {
        Serial.println("[WiFiScan] /api/wifi/scan-start called");
        Serial.flush();
        if (!_scanRunning) {
            _scanRunning = true;
            _scanReady   = false;
            _scanCount   = 0;
            BaseType_t ok = xTaskCreatePinnedToCore(
                wifiScanTask, "wifi_scan", 8192, nullptr, 1, nullptr, 0);
            Serial.printf("[WiFiScan] xTaskCreate: %s\n", ok == pdPASS ? "OK" : "FAIL");
            Serial.flush();
            if (ok != pdPASS) {
                _scanRunning = false;
                req->send(500, "application/json", "{\"error\":\"scan task failed\"}");
                return;
            }
        } else {
            Serial.println("[WiFiScan] scan already running");
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // WiFi scan — poll result
    _server.on("/api/wifi/scan-result", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!_scanReady) {
            req->send(200, "application/json", "{\"scanning\":true,\"networks\":[]}");
            return;
        }

        // Use the count captured by the task (avoids scanComplete() ambiguity
        // after a blocking scan run in a separate task/core)
        int n = _scanCount;
        JsonDocument doc;
        doc["scanning"] = false;
        JsonArray arr = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n && i < 30; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = WiFi.SSID(i);
            o["rssi"] = WiFi.RSSI(i);
            o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
        WiFi.scanDelete();
        _scanReady = false;
        _scanCount = 0;

        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

    // WiFi connect — save credentials and start connection
    _server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument inp;
            if (deserializeJson(inp, _body) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
                return;
            }
            String ssid = inp["ssid"] | "";
            String pass = inp["password"] | "";
            if (ssid.isEmpty()) {
                req->send(400, "application/json", "{\"error\":\"ssid required\"}");
                return;
            }
            wifi_manager.saveCredentials(ssid.c_str(), pass.c_str());
            wifi_manager.connectToWiFi(ssid.c_str(), pass.c_str());
            req->send(200, "application/json", "{\"ok\":true}");
        },
        nullptr, bodyCollect);

    // WiFi status — used by setup page to poll connection result
    _server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        wl_status_t st = WiFi.status();
        bool connected = (st == WL_CONNECTED);
        bool failed    = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
        JsonDocument doc;
        doc["connected"] = connected;
        doc["failed"]    = failed;
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
    _server.on("/api/test-print",           HTTP_POST, stub("{\"ok\":true,\"message\":\"Testdruck gesendet\"}"));
    _server.on("/api/buzzer-test",          HTTP_POST, stub("{\"ok\":true}"));
    _server.on("/api/logs",                 HTTP_GET,  stub("[]"));
    _server.on("/api/logs/clear",           HTTP_POST, stub("{\"ok\":true}"));
    _server.on("/api/scanner/ble-scan",     HTTP_GET,  stub("[]"));
    _server.on("/api/scanner/ble-connect",  HTTP_POST, stub("{\"ok\":true}"), nullptr, bodyCollect);
    _server.on("/api/ota-url",              HTTP_POST, stub("{\"ok\":true,\"message\":\"OTA gestartet\"}"), nullptr, bodyCollect);

    // ---- STATS ----
    _server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument inv, doc;
        loadJson("/inventory.json", inv, "[]");
        doc["totalStored"]    = inv.is<JsonArray>() ? inv.as<JsonArray>().size() : 0;
        doc["totalConsumed"]  = 0;
        doc["totalWasted"]    = 0;
        doc["avgStorageDays"] = 0;
        doc["topProducts"].to<JsonArray>();
        doc["categoryUsage"].to<JsonArray>();
        doc["history"].to<JsonArray>();
        String body;
        serializeJson(doc, body);
        req->send(200, "application/json", body);
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
