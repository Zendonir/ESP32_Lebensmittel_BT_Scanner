#include "SyncManager.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>
#include "../storage/AppFS.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

SyncManager sync_manager;

static constexpr const char *QUEUE_FILE  = "/sync_queue.json";
static constexpr const char *CONFIG_FILE = "/server_sync_config.json";

// Derive the sync_bridge URL from the stored IP
static String bridgeUrl(const String &ip) {
    if (ip.isEmpty()) return "";
    return "http://" + ip + "/sync_bridge.php";
}

void SyncManager::begin() {
    loadConfig();
    loadQueue();
    Logger::info("Sync", String("begin ip=") + _ip + " queued=" + _queue.size());
}

void SyncManager::loop() {
    if (_queue.empty()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (_ip.isEmpty()) return;

    uint32_t now = millis();
    if (now - _lastAttemptMs < RETRY_INTERVAL_MS) return;
    _lastAttemptMs = now;

    SyncEvent &ev = _queue.front();
    bool ok = postJson(ev.payload);
    if (ok) {
        Logger::info("Sync", String("sent type=") + ev.type);
        _lastSyncTime = time(nullptr);
        _lastSyncOk   = true;
        _queue.erase(_queue.begin());
        saveQueue();
    } else {
        ev.retries++;
        Logger::warn("Sync", String("failed retries=") + ev.retries);
        if (ev.retries >= MAX_RETRIES) {
            Logger::error("Sync", String("dropping event type=") + ev.type);
            _queue.erase(_queue.begin());
            saveQueue();
        }
    }
}

void SyncManager::enqueue(const String &type, const String &jsonPayload) {
    SyncEvent ev;
    ev.type      = type;
    ev.payload   = jsonPayload;
    ev.retries   = 0;
    ev.createdMs = millis();
    _queue.push_back(ev);
    saveQueue();
    _lastAttemptMs = 0;
}

void SyncManager::clearQueue() {
    _queue.clear();
    saveQueue();
}

String SyncManager::getQueueJson() const {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &ev : _queue) {
        JsonObject o = arr.add<JsonObject>();
        o["type"]    = ev.type;
        o["retries"] = ev.retries;
        JsonDocument inner;
        if (deserializeJson(inner, ev.payload) == DeserializationError::Ok) {
            o["data"] = inner["name"] | inner["labelBarcode"] | ev.type;
        } else {
            o["data"] = ev.payload.substring(0, 60);
        }
    }
    String out;
    serializeJson(doc, out);
    return out;
}

bool SyncManager::testConnection(String &outMsg) {
    if (_ip.isEmpty()) {
        outMsg = "Keine Server-IP konfiguriert";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        outMsg = "Kein WLAN";
        return false;
    }

    JsonDocument doc;
    doc["type"] = "PING";
    String body;
    serializeJson(doc, body);

    int code = 0;
    bool ok  = postJson(body, &code);
    outMsg = ok
        ? "Verbindung erfolgreich (HTTP " + String(code) + ")"
        : "Verbindung fehlgeschlagen (HTTP " + String(code) + ")";
    return ok;
}

// ---------- private ----------

void SyncManager::loadConfig() {
    if (!AppFS::fs().exists(CONFIG_FILE)) return;
    File f = AppFS::fs().open(CONFIG_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    _ip   = doc["ip"]   | "";
    _user = doc["user"] | "";
    _pass = doc["pass"] | "";
}

void SyncManager::saveQueue() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &ev : _queue) {
        JsonObject o = arr.add<JsonObject>();
        o["type"]      = ev.type;
        o["payload"]   = ev.payload;
        o["retries"]   = ev.retries;
        o["createdMs"] = ev.createdMs;
    }
    File f = AppFS::fs().open(QUEUE_FILE, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

void SyncManager::loadQueue() {
    if (!AppFS::fs().exists(QUEUE_FILE)) return;
    File f = AppFS::fs().open(QUEUE_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    _queue.clear();
    for (JsonObject o : doc.as<JsonArray>()) {
        SyncEvent ev;
        ev.type      = o["type"]      | "";
        ev.payload   = o["payload"]   | "";
        ev.retries   = o["retries"]   | 0;
        ev.createdMs = o["createdMs"] | 0u;
        if (!ev.type.isEmpty() && !ev.payload.isEmpty())
            _queue.push_back(ev);
    }
}

bool SyncManager::postJson(const String &json, int *outCode) {
    String url = bridgeUrl(_ip);
    HTTPClient http;
    http.setTimeout(8000);
    http.setReuse(false);

    bool ok   = false;
    int  code = 0;

    WiFiClient client;
    if (http.begin(client, url)) {
        http.addHeader("Content-Type", "application/json");
        // Send credentials as headers — bridge uses them to connect to MySQL
        http.addHeader("X-DB-User", _user);
        http.addHeader("X-DB-Pass", _pass);
        code = http.POST((uint8_t *)json.c_str(), json.length());
        http.end();
        ok = (code >= 200 && code < 300);
    }

    if (outCode) *outCode = code;
    return ok;
}
