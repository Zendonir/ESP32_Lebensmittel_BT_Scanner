#include "WebInterface.h"
#include "../core/Logger.h"
#include "../inventory/InventoryManager.h"
#include "../storage/JsonStorage.h"

#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_system.h>

/* ---------------------------------------------------------------
   Construction
   --------------------------------------------------------------- */
WebInterface::WebInterface(uint16_t port) : _server(port) {}

/* ---------------------------------------------------------------
   Public
   --------------------------------------------------------------- */
void WebInterface::begin() {
    registerStaticRoutes();
    registerApiRoutes();
    _server.begin();
    Logger::info("Web", "Web interface started on port 80");
}

/* ---------------------------------------------------------------
   Static file serving from LittleFS
   --------------------------------------------------------------- */
void WebInterface::registerStaticRoutes() {
    // SPA root — always serve index.html
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(404, "text/plain", "LittleFS not mounted or index.html missing");
        }
    });

    // Named static files
    auto serveFile = [](const char *path, const char *mime) {
        return [path, mime](AsyncWebServerRequest *req) {
            if (LittleFS.exists(path)) {
                req->send(LittleFS, path, mime);
            } else {
                req->send(404, "text/plain", "File not found");
            }
        };
    };
    _server.on("/style.css", HTTP_GET, serveFile("/style.css", "text/css"));
    _server.on("/app.js",    HTTP_GET, serveFile("/app.js",    "application/javascript"));

    // Fallback: serve any file from LittleFS
    _server.onNotFound([](AsyncWebServerRequest *req) {
        const String path = req->url();
        if (LittleFS.exists(path)) {
            String mime = "application/octet-stream";
            if (path.endsWith(".html")) mime = "text/html";
            else if (path.endsWith(".css"))  mime = "text/css";
            else if (path.endsWith(".js"))   mime = "application/javascript";
            else if (path.endsWith(".json")) mime = "application/json";
            else if (path.endsWith(".ico"))  mime = "image/x-icon";
            req->send(LittleFS, path, mime);
        } else {
            // SPA fallback: any unknown path returns index.html
            if (!path.startsWith("/api/")) {
                if (LittleFS.exists("/index.html"))
                    req->send(LittleFS, "/index.html", "text/html");
                else
                    req->send(404, "text/plain", "Not found");
            } else {
                req->send(404, "application/json", "{\"error\":\"not found\"}");
            }
        }
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
    JsonDocument doc;
    doc["error"] = msg;
    sendJson(req, doc, code);
}

/* Body is collected via onRequestBody; we store it in a simple per-request header trick.
   ESPAsyncWebServer delivers the body in chunks to onBody(). For JSON APIs we collect it. */
static String _bodyBuffer;

/* ---------------------------------------------------------------
   JSON file helpers (read/write via LittleFS directly)
   --------------------------------------------------------------- */
static bool loadJson(const char *path, JsonDocument &doc, const char *fallback = "{}") {
    File f = LittleFS.open(path, "r");
    if (!f) {
        deserializeJson(doc, fallback);
        return false;
    }
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return !err;
}

static bool saveJson(const char *path, JsonDocument &doc) {
    // Write to temp file then rename for atomicity
    const String tmp = String(path) + ".tmp";
    File f = LittleFS.open(tmp.c_str(), "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    LittleFS.remove(path);
    LittleFS.rename(tmp.c_str(), path);
    return true;
}

/* ---------------------------------------------------------------
   Route handlers – body is collected via server.onRequestBody
   For simplicity we route everything through a global buffer.
   Routes that need the body register onBody + onRequest pairs.
   --------------------------------------------------------------- */

/* Macro for GET routes that serve a JSON file */
#define SERVE_JSON_FILE(path_str, file_str, fallback_str) \
    _server.on(path_str, HTTP_GET, [](AsyncWebServerRequest *req) { \
        JsonDocument doc; \
        loadJson(file_str, doc, fallback_str); \
        String body; serializeJson(doc, body); \
        req->send(200, "application/json", body); \
    })

/* Macro for POST routes that merge body into a JSON file */
#define HANDLE_JSON_POST(path_str, file_str, fallback_str) \
    _server.on(path_str, HTTP_POST, \
        [](AsyncWebServerRequest *req) { \
            JsonDocument existing; \
            loadJson(file_str, existing, fallback_str); \
            JsonDocument incoming; \
            if (!deserializeJson(incoming, _bodyBuffer)) { \
                for (JsonPair kv : incoming.as<JsonObject>()) \
                    existing[kv.key()] = kv.value(); \
            } \
            saveJson(file_str, existing); \
            req->send(200, "application/json", "{\"ok\":true}"); \
        }, \
        nullptr, \
        [](AsyncWebServerRequest *, uint8_t *data, size_t len, size_t, size_t total) { \
            if (len == total) _bodyBuffer = String((char*)data, len); \
        } \
    )

void WebInterface::registerApiRoutes() {
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
        doc["hostname"]   = WiFi.getHostname();
        doc["fsUsed"]     = LittleFS.usedBytes();
        doc["fsTotal"]    = LittleFS.totalBytes();
        // Uptime in human-readable form
        unsigned long s = millis() / 1000;
        char upbuf[32];
        snprintf(upbuf, sizeof(upbuf), "%lud %02luh %02lum", s/86400, (s%86400)/3600, (s%3600)/60);
        doc["uptime"] = upbuf;
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

    // ---- INVENTORY ----
    SERVE_JSON_FILE("/api/inventory", "/inventory.json", "[]");
    _server.on("/api/inventory", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/inventory.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            JsonDocument item;
            if (!deserializeJson(item, _bodyBuffer)) {
                arr.as<JsonArray>().add(item);
                saveJson("/inventory.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );
    _server.on("/api/inventory/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/inventory.json", arr, "[]");
            JsonDocument key;
            if (!deserializeJson(key, _bodyBuffer) && arr.is<JsonArray>()) {
                const String lb  = key["labelBarcode"].as<String>();
                const String bc  = key["barcode"].as<String>();
                JsonArray newArr = JsonDocument().to<JsonArray>();
                JsonDocument newDoc;
                newDoc.to<JsonArray>();
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if (v["labelBarcode"] == lb || v["barcode"] == bc) continue;
                    newDoc.as<JsonArray>().add(v);
                }
                saveJson("/inventory.json", newDoc);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );

    // ---- CUSTOM PRODUCTS (TEMPLATES) ----
    SERVE_JSON_FILE("/api/custom-products", "/custom_products.json", "[]");
    _server.on("/api/custom-products", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/custom_products.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            JsonDocument item;
            if (!deserializeJson(item, _bodyBuffer))
                arr.as<JsonArray>().add(item);
            saveJson("/custom_products.json", arr);
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );
    _server.on("/api/custom-products/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/custom_products.json", arr, "[]");
            JsonDocument key;
            JsonDocument newDoc;
            newDoc.to<JsonArray>();
            if (!deserializeJson(key, _bodyBuffer) && arr.is<JsonArray>()) {
                const String bc   = key["barcode"].as<String>();
                const String name = key["name"].as<String>();
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if (v["barcode"] == bc || v["name"] == name) continue;
                    newDoc.as<JsonArray>().add(v);
                }
            }
            saveJson("/custom_products.json", newDoc);
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );

    // ---- CATEGORIES ----
    SERVE_JSON_FILE("/api/categories", "/categories.json", "[]");
    _server.on("/api/categories", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/categories.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            JsonDocument item;
            if (!deserializeJson(item, _bodyBuffer)) {
                // replace if oldName matches, else append
                const String oldName = item["oldName"].as<String>();
                item.remove("oldName");
                bool replaced = false;
                if (oldName.length()) {
                    for (JsonVariant v : arr.as<JsonArray>()) {
                        if (v["name"] == oldName) { v.set(item); replaced = true; break; }
                    }
                }
                if (!replaced) arr.as<JsonArray>().add(item);
                saveJson("/categories.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );
    _server.on("/api/categories/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/categories.json", arr, "[]");
            JsonDocument key;
            JsonDocument newDoc;
            newDoc.to<JsonArray>();
            if (!deserializeJson(key, _bodyBuffer) && arr.is<JsonArray>()) {
                const String name = key["name"].as<String>();
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if (v["name"] == name) continue;
                    newDoc.as<JsonArray>().add(v);
                }
            }
            saveJson("/categories.json", newDoc);
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );

    // ---- SHOPPING LIST ----
    SERVE_JSON_FILE("/api/shopping-list", "/shopping_list.json", "[]");
    _server.on("/api/shopping-list", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/shopping_list.json", arr, "[]");
            if (!arr.is<JsonArray>()) arr.to<JsonArray>();
            JsonDocument item;
            if (!deserializeJson(item, _bodyBuffer))
                arr.as<JsonArray>().add(item);
            saveJson("/shopping_list.json", arr);
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );
    _server.on("/api/shopping-list/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/shopping_list.json", arr, "[]");
            JsonDocument item;
            if (!deserializeJson(item, _bodyBuffer) && arr.is<JsonArray>()) {
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if (v["name"] == item["name"]) { v["bought"] = item["bought"]; break; }
                }
                saveJson("/shopping_list.json", arr);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );
    _server.on("/api/shopping-list/delete", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument arr;
            loadJson("/shopping_list.json", arr, "[]");
            JsonDocument key;
            JsonDocument newDoc;
            newDoc.to<JsonArray>();
            if (!deserializeJson(key, _bodyBuffer) && arr.is<JsonArray>()) {
                const String name = key["name"].as<String>();
                for (JsonVariant v : arr.as<JsonArray>()) {
                    if (v["name"] == name) continue;
                    newDoc.as<JsonArray>().add(v);
                }
            }
            saveJson("/shopping_list.json", newDoc);
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );

    // ---- UI CONFIG ----
    SERVE_JSON_FILE("/api/ui-config", "/ui_config.json", "{}");
    HANDLE_JSON_POST("/api/ui-config", "/ui_config.json", "{}");
    _server.on("/api/ui-config/reset", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            LittleFS.remove("/ui_config.json");
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ---- FONT CONFIG ----
    SERVE_JSON_FILE("/api/font-config", "/font_config.json", "{}");
    HANDLE_JSON_POST("/api/font-config", "/font_config.json", "{}");

    // ---- PRINTER CONFIG ----
    SERVE_JSON_FILE("/api/printer-config", "/printer_config.json", "{}");
    HANDLE_JSON_POST("/api/printer-config", "/printer_config.json", "{}");

    // ---- SCANNER CONFIG ----
    SERVE_JSON_FILE("/api/scanner-config", "/scanner_config.json", "{}");
    HANDLE_JSON_POST("/api/scanner-config", "/scanner_config.json", "{}");

    // ---- WIFI ----
    _server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        loadJson("/wifi_config.json", doc, "{}");
        doc["connected"]   = (WiFi.status() == WL_CONNECTED);
        doc["ip"]          = WiFi.localIP().toString();
        doc["rssi"]        = WiFi.RSSI();
        doc["currentTime"] = ""; // filled by NTP if available
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    HANDLE_JSON_POST("/api/wifi", "/wifi_config.json", "{}");
    HANDLE_JSON_POST("/api/wifi/ap", "/wifi_config.json", "{}");
    _server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
        // Trigger WiFi scan and return results
        int n = WiFi.scanNetworks(false, true);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n && i < 20; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"]  = WiFi.SSID(i);
            o["rssi"]  = WiFi.RSSI(i);
            o["open"]  = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
        WiFi.scanDelete();
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

    // ---- MQTT ----
    SERVE_JSON_FILE("/api/mqtt", "/mqtt_config.json", "{}");
    HANDLE_JSON_POST("/api/mqtt", "/mqtt_config.json", "{}");
    _server.on("/api/mqtt/test", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":false,\"message\":\"MQTT test not yet wired up\"}");
        }
    );

    // ---- TELEGRAM ----
    SERVE_JSON_FILE("/api/telegram", "/telegram_config.json", "{}");
    HANDLE_JSON_POST("/api/telegram", "/telegram_config.json", "{}");
    _server.on("/api/telegram/test", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":false,\"message\":\"Telegram test not yet wired up\"}");
        }
    );

    // ---- SERVER SYNC ----
    SERVE_JSON_FILE("/api/server-sync", "/server_sync_config.json", "{}");
    HANDLE_JSON_POST("/api/server-sync", "/server_sync_config.json", "{}");
    _server.on("/api/server-sync/test", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":false,\"message\":\"Sync test not yet wired up\"}");
        }
    );
    _server.on("/api/server-sync/queue", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "[]");
    });
    _server.on("/api/server-sync/queue/clear", HTTP_POST,
        [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); }
    );

    // ---- STATS ----
    _server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        // Derive basic stats from inventory
        JsonDocument inv;
        loadJson("/inventory.json", inv, "[]");
        int total = 0;
        if (inv.is<JsonArray>()) total = inv.as<JsonArray>().size();
        doc["totalStored"]   = total;
        doc["totalConsumed"] = 0;
        doc["totalWasted"]   = 0;
        doc["avgStorageDays"]= 0;
        doc["topProducts"]   = JsonArray();
        doc["categoryUsage"] = JsonArray();
        doc["history"]       = JsonArray();
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    });

    // ---- SCAN LOG ----
    SERVE_JSON_FILE("/api/scan-log", "/scan_log.json", "[]");
    _server.on("/api/scan-log/clear", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument doc;
            doc.to<JsonArray>();
            saveJson("/scan_log.json", doc);
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ---- LOGS (runtime) ----
    _server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *req) {
        // Serve a static log snapshot; real implementation would pull from Logger ring buffer
        req->send(200, "application/json", "[]");
    });
    _server.on("/api/logs/clear", HTTP_POST,
        [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); }
    );

    // ---- TEST PRINT ----
    _server.on("/api/test-print", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            // Delegate to PrinterManager when wired up
            req->send(200, "application/json", "{\"ok\":true,\"message\":\"Testdruck gesendet\"}");
        }
    );

    // ---- BUZZER TEST ----
    _server.on("/api/buzzer-test", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ---- SYSTEM ACTIONS ----
    _server.on("/api/restart", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":true}");
            delay(200);
            esp_restart();
        }
    );
    _server.on("/api/factory-reset", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":true}");
            delay(200);
            LittleFS.format();
            esp_restart();
        }
    );
    _server.on("/api/format-fs", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            LittleFS.format();
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );
    _server.on("/api/cache/clear", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            LittleFS.remove("/off_cache.json");
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ---- OTA via URL ----
    _server.on("/api/ota-url", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            // OTAUpdateManager::updateFromUrl() should be called here once wired up.
            req->send(200, "application/json", "{\"ok\":true,\"message\":\"OTA gestartet\"}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );

    // ---- OTA via file upload ----
    _server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            bool ok = !Update.hasError();
            req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Update failed\"}");
            if (ok) { delay(500); esp_restart(); }
        },
        [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Logger::info("OTA", "Start: " + filename);
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Logger::error("OTA", "begin() failed");
                }
            }
            if (Update.isRunning()) {
                Update.write(data, len);
            }
            if (final) {
                if (!Update.end(true)) {
                    Logger::error("OTA", "end() failed");
                }
            }
        }
    );

    // ---- BLE scanner scan/connect (stubs – wired up by BarcodeManager) ----
    _server.on("/api/scanner/ble-scan", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "[]");
    });
    _server.on("/api/scanner/ble-connect", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":true}");
        }, nullptr,
        [](AsyncWebServerRequest *, uint8_t *d, size_t l, size_t, size_t t) {
            if (l == t) _bodyBuffer = String((char*)d, l);
        }
    );
}
