#include "SyncManager.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>
#include "../storage/AppFS.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

SyncManager sync_manager;

static constexpr const char *QUEUE_FILE  = "/sync_queue.json";
static constexpr const char *CONFIG_FILE = "/server_sync_config.json";

// SQL string escaping: replace ' with '' and \ with \\
static String sqlEsc(const String &s) {
    String r;
    r.reserve(s.length());
    for (char c : s) {
        if (c == '\'') r += "''";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
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

    // Parse the event payload JSON
    JsonDocument doc;
    if (deserializeJson(doc, ev.payload) != DeserializationError::Ok) {
        Logger::error("Sync", String("bad payload type=") + ev.type);
        _queue.erase(_queue.begin());
        saveQueue();
        return;
    }

    String sql;

    if (ev.type == "ADD") {
        String labelBarcode = sqlEsc(doc["labelBarcode"] | "");
        String barcode      = sqlEsc(doc["barcode"]      | "");
        String name         = sqlEsc(doc["name"]         | "");
        String brand        = sqlEsc(doc["brand"]        | "");
        String category     = sqlEsc(doc["category"]     | "");
        String expiryDate   = sqlEsc(doc["expiryDate"]   | "");
        String addedDate    = sqlEsc(doc["addedDate"]    | "");
        int    quantity     = doc["quantity"]             | 1;
        String household    = sqlEsc(doc["household"]    | "");
        String deviceName   = sqlEsc(doc["deviceName"]   | "");

        sql  = "INSERT INTO `Lebensmittel_Scanner`.`inventar` ";
        sql += "(`label_barcode`,`barcode`,`name`,`brand`,`category`,`expiry_date`,`added_date`,`quantity`,`household`,`device_name`) ";
        sql += "VALUES ('";
        sql += labelBarcode; sql += "','";
        sql += barcode;      sql += "','";
        sql += name;         sql += "','";
        sql += brand;        sql += "','";
        sql += category;     sql += "','";
        sql += expiryDate;   sql += "','";
        sql += addedDate;    sql += "',";
        sql += quantity;     sql += ",'";
        sql += household;    sql += "','";
        sql += deviceName;   sql += "') ";
        sql += "ON DUPLICATE KEY UPDATE ";
        sql += "`barcode`=VALUES(`barcode`),`name`=VALUES(`name`),`brand`=VALUES(`brand`),";
        sql += "`category`=VALUES(`category`),`expiry_date`=VALUES(`expiry_date`),";
        sql += "`added_date`=VALUES(`added_date`),`quantity`=VALUES(`quantity`),";
        sql += "`household`=VALUES(`household`),`device_name`=VALUES(`device_name`)";

    } else if (ev.type == "REMOVE_LABEL") {
        String labelBarcode = sqlEsc(doc["labelBarcode"] | "");
        sql  = "DELETE FROM `Lebensmittel_Scanner`.`inventar` WHERE `label_barcode`='";
        sql += labelBarcode;
        sql += "'";

    } else {
        Logger::warn("Sync", String("unknown event type=") + ev.type + ", dropping");
        _queue.erase(_queue.begin());
        saveQueue();
        return;
    }

    bool ok = execDirectMySQL(sql);
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

    IPAddress serverIP;
    if (!serverIP.fromString(_ip)) {
        outMsg = "Ungültige Server-IP: " + _ip;
        return false;
    }

    char userBuf[64], passBuf[64];
    strncpy(userBuf, _user.c_str(), 63); userBuf[63] = 0;
    strncpy(passBuf, _pass.c_str(), 63); passBuf[63] = 0;

    WiFiClient wc;
    wc.setTimeout(5000);
    MySQL_Connection conn((Client*)&wc);

    bool ok = conn.connect(serverIP, 3306, userBuf, passBuf);
    if (ok) {
        conn.close();
        outMsg = "MySQL-Verbindung erfolgreich";
    } else {
        outMsg = "MySQL-Verbindung fehlgeschlagen";
    }
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

bool SyncManager::execDirectMySQL(const String &sql) {
    if (_ip.isEmpty()) return false;
    IPAddress serverIP;
    if (!serverIP.fromString(_ip)) return false;

    char userBuf[64], passBuf[64];
    strncpy(userBuf, _user.c_str(), 63); userBuf[63] = 0;
    strncpy(passBuf, _pass.c_str(), 63); passBuf[63] = 0;

    WiFiClient wc;
    wc.setTimeout(5000);
    MySQL_Connection conn((Client*)&wc);

    if (!conn.connect(serverIP, 3306, userBuf, passBuf)) {
        Logger::warn("Sync", "MySQL connect failed");
        return false;
    }
    MySQL_Cursor cur(&conn);
    bool ok = cur.execute(sql.c_str());
    conn.close();
    return ok;
}
