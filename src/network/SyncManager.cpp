#include "SyncManager.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

SyncManager sync_manager;

static constexpr const char *QUEUE_FILE  = "/sync_queue.json";
static constexpr const char *CONFIG_FILE = "/server_sync_config.json";

void SyncManager::begin() {
    loadConfig();
    loadQueue();
    Logger::info("Sync", String("begin url=") + _url + " queued=" + _queue.size());
}

void SyncManager::loop() {
    if (_queue.empty()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (_url.isEmpty()) return;

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
    _lastAttemptMs = 0; // trigger immediate send attempt next loop
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
        // include a shortened preview of the payload
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
    if (_url.isEmpty()) {
        outMsg = "Keine Server-URL konfiguriert";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        outMsg = "Kein WLAN";
        return false;
    }

    // POST a ping event to test reachability
    JsonDocument doc;
    doc["type"]       = "PING";
    doc["deviceId"]   = _deviceId;
    doc["deviceName"] = _deviceName;
    doc["household"]  = _householdId;
    String body;
    serializeJson(doc, body);

    int code = 0;
    bool ok  = postJson(body, &code);
    if (ok) {
        outMsg = "Verbindung erfolgreich (HTTP " + String(code) + ")";
    } else {
        outMsg = "Verbindung fehlgeschlagen (HTTP " + String(code) + ")";
    }
    return ok;
}

// ---------- private ----------

void SyncManager::loadConfig() {
    if (!LittleFS.exists(CONFIG_FILE)) return;
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    _url        = doc["url"]         | "";
    _deviceId   = doc["deviceId"]    | "";
    _deviceName = doc["deviceName"]  | "";
    _room       = doc["room"]        | "";
    _householdId = doc["householdId"] | "";
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
    File f = LittleFS.open(QUEUE_FILE, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

void SyncManager::loadQueue() {
    if (!LittleFS.exists(QUEUE_FILE)) return;
    File f = LittleFS.open(QUEUE_FILE, "r");
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
    HTTPClient http;
    http.setTimeout(8000);
    http.setReuse(false);

    bool ok = false;
    int  code = 0;

    if (_url.startsWith("https://")) {
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(8);
        if (http.begin(client, _url)) {
            http.addHeader("Content-Type", "application/json");
            http.addHeader("X-Device-Id", _deviceId);
            code = http.POST((uint8_t *)json.c_str(), json.length());
            http.end();
            ok = (code >= 200 && code < 300);
        }
    } else {
        WiFiClient client;
        if (http.begin(client, _url)) {
            http.addHeader("Content-Type", "application/json");
            http.addHeader("X-Device-Id", _deviceId);
            code = http.POST((uint8_t *)json.c_str(), json.length());
            http.end();
            ok = (code >= 200 && code < 300);
        }
    }

    if (outCode) *outCode = code;
    return ok;
}
