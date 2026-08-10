#include "SyncManager.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>
#include "../storage/AppFS.h"
#include <WiFi.h>
#include "MySQLDirect.h"

SyncManager sync_manager;

static constexpr const char *QUEUE_FILE  = "/sync_queue.json";
static constexpr const char *CONFIG_FILE = "/server_sync_config.json";

// RAII scoped lock for the queue mutex.
namespace {
struct QLock {
    SemaphoreHandle_t m;
    explicit QLock(SemaphoreHandle_t mm) : m(mm) { if (m) xSemaphoreTake(m, portMAX_DELAY); }
    ~QLock()                                      { if (m) xSemaphoreGive(m); }
};
}

static String sqlEsc(const String &s) {
    String r;
    r.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '\'') r += "''";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

void SyncManager::begin() {
    if (!_queueMutex) _queueMutex = xSemaphoreCreateMutex();
    loadConfig();
    loadQueue();   // runs before other tasks start; no lock needed
    Logger::info("Sync", String("begin ip=") + _ip + " queued=" + _queue.size());

    // Eigener Worker-Task auf Core 0. Der MySQL-Zugriff blockiert bis zu mehrere
    // Sekunden (TCP-Connect-Timeout, Read-Timeout). Lief er wie bisher direkt in
    // App::loop(), stand die komplette UI währenddessen still – das erklärt das
    // periodische "Einfrieren", sobald der Datenbankserver nicht erreichbar ist.
    if (!_workerStarted) {
        if (xTaskCreatePinnedToCore(workerTaskFn, "sync_wk", 8192, this, 1, nullptr, 0) == pdPASS)
            _workerStarted = true;
        else
            Logger::error("Sync", "Worker-Task konnte nicht gestartet werden");
    }
}

/* static */ void SyncManager::workerTaskFn(void *arg) {
    SyncManager *self = static_cast<SyncManager *>(arg);
    for (;;) {
        self->processQueueOnce();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Drop the current front event iff it is still the one we just processed.
// (clearQueue() from the web task may have emptied/changed the queue while we
//  were blocked in MySQL.)
void SyncManager::dropFrontIfMatches(const String &payload) {
    QLock lk(_queueMutex);
    if (!_queue.empty() && _queue.front().payload == payload) {
        _queue.erase(_queue.begin());
        saveQueue();
    }
}

void SyncManager::processQueueOnce() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (_ip.isEmpty()) return;

    uint32_t now = millis();
    if (now - _lastAttemptMs < RETRY_INTERVAL_MS) return;

    // Snapshot the front event under lock; do the blocking MySQL work WITHOUT
    // holding the lock so a concurrent web getQueueJson()/clearQueue() can't
    // race with an erase (vector realloc → use-after-free).
    SyncEvent ev;
    {
        QLock lk(_queueMutex);
        if (_queue.empty()) return;
        ev = _queue.front();   // copy
    }
    _lastAttemptMs = now;

    JsonDocument doc;
    if (deserializeJson(doc, ev.payload) != DeserializationError::Ok) {
        Logger::error("Sync", String("bad payload type=") + ev.type);
        dropFrontIfMatches(ev.payload);
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
        int    qty          = doc["quantity"]             | 1;
        String household    = sqlEsc(doc["household"]    | "");
        String deviceName   = sqlEsc(doc["deviceName"]   | "");
        String location     = sqlEsc(doc["location"]     | "");

        sql  = "INSERT INTO `Lebensmittel_Scanner`.`inventar`";
        sql += " (`label_barcode`,`barcode`,`name`,`brand`,`category`,";
        sql += "`expiry_date`,`added_date`,`quantity`,`household`,`device_name`,`location`)";
        sql += " VALUES ('";
        sql += labelBarcode; sql += "','";
        sql += barcode;      sql += "','";
        sql += name;         sql += "','";
        sql += brand;        sql += "','";
        sql += category;     sql += "','";
        sql += expiryDate;   sql += "','";
        sql += addedDate;    sql += "',";
        sql += qty;          sql += ",'";
        sql += household;    sql += "','";
        sql += deviceName;   sql += "','";
        sql += location;     sql += "')";
        sql += " ON DUPLICATE KEY UPDATE";
        sql += " `barcode`=VALUES(`barcode`),`name`=VALUES(`name`),";
        sql += "`brand`=VALUES(`brand`),`category`=VALUES(`category`),";
        sql += "`expiry_date`=VALUES(`expiry_date`),`added_date`=VALUES(`added_date`),";
        sql += "`quantity`=VALUES(`quantity`),`household`=VALUES(`household`),";
        sql += "`device_name`=VALUES(`device_name`),`location`=VALUES(`location`)";

    } else if (ev.type == "REMOVE_LABEL") {
        String lb        = sqlEsc(doc["labelBarcode"] | "");
        String household = sqlEsc(doc["household"]    | "");
        String devName   = sqlEsc(doc["deviceName"]   | "");
        // Delete from inventar
        sql  = "DELETE FROM `Lebensmittel_Scanner`.`inventar` WHERE `label_barcode`='";
        sql += lb;
        sql += "'";
        // Also write tombstone so other devices know about the removal
        String tomb  = "INSERT INTO `Lebensmittel_Scanner`.`removed_items`";
        tomb += " (`label_barcode`,`household`,`removed_by`)";
        tomb += " VALUES ('"; tomb += lb; tomb += "','";
        tomb += household;   tomb += "','";
        tomb += devName;     tomb += "')";
        tomb += " ON DUPLICATE KEY UPDATE `removed_by`=VALUES(`removed_by`),`removed_at`=NOW()";
        execDirectMySQL(tomb);   // best-effort; main result is the DELETE below

    } else if (ev.type == "REBUILD_HOUSEHOLD") {
        // Atomically replace all MySQL rows for this household with the compacted set.
        String household  = sqlEsc(doc["household"]  | "");
        String deviceName = sqlEsc(doc["deviceName"] | "");
        // 1. Delete all existing inventory rows for this household
        bool ok = execDirectMySQL(
            String("DELETE FROM `Lebensmittel_Scanner`.`inventar` WHERE `household`='") + household + "'"
        );
        if (!ok) {
            QLock lk(_queueMutex);
            if (!_queue.empty() && _queue.front().payload == ev.payload) {
                _queue.front().retries++;
                Logger::warn("Sync", String("REBUILD_HOUSEHOLD delete failed, retries=") + _queue.front().retries);
                if (_queue.front().retries >= MAX_RETRIES) {
                    Logger::error("Sync", "REBUILD_HOUSEHOLD dropped after max retries");
                    _queue.erase(_queue.begin());
                    saveQueue();
                }
            }
            return;
        }
        // 2. Clear stale tombstones so pulled items aren't wrongly removed on next pull
        execDirectMySQL(
            String("DELETE FROM `Lebensmittel_Scanner`.`removed_items` WHERE `household`='") + household + "'"
        );
        // 3. Re-insert all surviving items
        JsonArray jItems = doc["items"].as<JsonArray>();
        for (JsonObject jItem : jItems) {
            String lb  = sqlEsc(jItem["labelBarcode"] | "");
            String bc  = sqlEsc(jItem["barcode"]      | "");
            String nm  = sqlEsc(jItem["name"]         | "");
            String br  = sqlEsc(jItem["brand"]        | "");
            String cat = sqlEsc(jItem["category"]     | "");
            String exp = sqlEsc(jItem["expiryDate"]   | "");
            String add = sqlEsc(jItem["addedDate"]    | "");
            int    qty = jItem["quantity"]              | 1;
            String loc = sqlEsc(jItem["location"]     | "");
            String ins  = "INSERT INTO `Lebensmittel_Scanner`.`inventar`";
            ins += " (`label_barcode`,`barcode`,`name`,`brand`,`category`,";
            ins += "`expiry_date`,`added_date`,`quantity`,`household`,`device_name`,`location`)";
            ins += " VALUES ('";
            ins += lb;         ins += "','";
            ins += bc;         ins += "','";
            ins += nm;         ins += "','";
            ins += br;         ins += "','";
            ins += cat;        ins += "','";
            ins += exp;        ins += "','";
            ins += add;        ins += "',";
            ins += qty;        ins += ",'";
            ins += household;  ins += "','";
            ins += deviceName; ins += "','";
            ins += loc;        ins += "') ON DUPLICATE KEY UPDATE `quantity`=VALUES(`quantity`)";
            execDirectMySQL(ins);  // best-effort per item
        }
        Logger::info("Sync", String("REBUILD_HOUSEHOLD done: ") + (int)jItems.size() + " items");
        {
            QLock lk(_queueMutex);
            if (!_queue.empty() && _queue.front().payload == ev.payload) {
                _queue.erase(_queue.begin());
                saveQueue();
            }
        }
        _lastSyncTime = time(nullptr);
        _lastSyncOk   = true;
        return;

    } else {
        Logger::warn("Sync", String("unknown event type=") + ev.type + ", dropping");
        dropFrontIfMatches(ev.payload);
        return;
    }

    bool ok = execDirectMySQL(sql);

    QLock lk(_queueMutex);
    // The event may have been cleared/changed by the web task while we were in MySQL.
    if (_queue.empty() || _queue.front().payload != ev.payload) return;

    if (ok) {
        Logger::info("Sync", String("sent type=") + ev.type);
        _lastSyncTime = time(nullptr);
        _lastSyncOk   = true;
        _queue.erase(_queue.begin());
        saveQueue();
    } else {
        SyncEvent &front = _queue.front();
        front.retries++;
        Logger::warn("Sync", String("failed retries=") + front.retries);
        if (front.retries >= MAX_RETRIES) {
            Logger::error("Sync", String("dropping event type=") + ev.type);
            _queue.erase(_queue.begin());
            saveQueue();
        }
    }
}

void SyncManager::enqueue(const String &type, const String &jsonPayload) {
    QLock lk(_queueMutex);
    static constexpr size_t MAX_QUEUE_SIZE = 50;
    if (_queue.size() >= MAX_QUEUE_SIZE) {
        Logger::warn("Sync", String("Queue voll – DROP: ") + type);
        return;
    }
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
    QLock lk(_queueMutex);
    _queue.clear();
    saveQueue();
}

void SyncManager::enqueueRebuild(const std::vector<InventoryItem> &items,
                                  const String &household, const String &deviceName) {
    JsonDocument doc;
    doc["household"]  = household;
    doc["deviceName"] = deviceName;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const auto &it : items) {
        JsonObject obj = arr.add<JsonObject>();
        obj["labelBarcode"] = it.labelBarcode;
        obj["barcode"]      = it.barcode;
        obj["name"]         = it.name;
        obj["brand"]        = it.brand;
        obj["category"]     = it.category;
        obj["expiryDate"]   = it.expiryDate;
        obj["addedDate"]    = it.addedDate;
        obj["quantity"]     = it.quantity;
        obj["unit"]         = it.unit;
        obj["location"]     = it.location;
    }
    String payload;
    serializeJson(doc, payload);
    enqueue("REBUILD_HOUSEHOLD", payload);
}

size_t SyncManager::pending() const {
    QLock lk(_queueMutex);
    return _queue.size();
}

String SyncManager::getQueueJson() const {
    QLock lk(_queueMutex);
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
    if (_ip.isEmpty()) { outMsg = "Keine Server-IP konfiguriert"; return false; }
    if (WiFi.status() != WL_CONNECTED) { outMsg = "Kein WLAN"; return false; }
    // 3-second timeout: this runs synchronously in the ESPAsyncWebServer handler,
    // blocking ALL other HTTP requests. Keeping it tight ensures other requests
    // (like GET /api/server-sync) can still complete within the 10s JS timeout.
    MySQLDirect db;
    bool ok = db.connect(_ip, 3306, _user, _pass, 3000);
    outMsg = ok ? "MySQL-Verbindung erfolgreich"
                : ("Verbindung fehlgeschlagen: " + db.lastError());
    if (ok) db.close();
    return ok;
}

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
    _syncIntervalMin = doc["syncIntervalMin"] | 10u;
    if (_syncIntervalMin < 1)  _syncIntervalMin = 1;
    if (_syncIntervalMin > 60) _syncIntervalMin = 60;
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
    MySQLDirect db;
    // Explizites Timeout: der Standardwert war deutlich länger und hielt den
    // Worker unnötig lange fest, wenn der Server nicht antwortet.
    if (!db.connect(_ip, 3306, _user, _pass, 3000)) {
        Logger::warn("Sync", String("MySQL connect failed: ") + db.lastError());
        return false;
    }
    bool ok = db.execute(sql);
    if (!ok) Logger::warn("Sync", String("MySQL exec failed: ") + db.lastError());
    db.close();
    return ok;
}

// ---------- product cache helpers -------------------------------------------

String SyncManager::labelsToString(const std::vector<String> &labels) {
    String s;
    for (size_t i = 0; i < labels.size(); i++) {
        if (i) s += ',';
        s += labels[i];
    }
    return s;
}

void SyncManager::labelsFromString(const String &s, std::vector<String> &out) {
    out.clear();
    if (s.isEmpty()) return;
    int start = 0;
    for (int i = 0; i <= (int)s.length(); i++) {
        if (i == (int)s.length() || s[i] == ',') {
            String label = s.substring(start, i);
            if (!label.isEmpty()) out.push_back(label);
            start = i + 1;
        }
    }
}

// ---------- product cache operations ----------------------------------------

bool SyncManager::pushProduct(const ProductInfo &product) {
    if (_ip.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
    if (product.barcode.isEmpty() || product.name.isEmpty()) return false;

    String sql =
        "INSERT INTO `Lebensmittel_Scanner`.`product_cache` "
        "(`barcode`,`name`,`brand`,`quantity`,`category`,`nutriscore`,`labels`) VALUES ('";
    sql += sqlEsc(product.barcode);   sql += "','";
    sql += sqlEsc(product.name);      sql += "','";
    sql += sqlEsc(product.brand);     sql += "','";
    sql += sqlEsc(product.quantity);  sql += "','";
    sql += sqlEsc(product.category);  sql += "','";
    sql += sqlEsc(product.nutriscore);sql += "','";
    sql += sqlEsc(labelsToString(product.labels)); sql += "') ";
    sql += "ON DUPLICATE KEY UPDATE "
           "`name`=VALUES(`name`),`brand`=VALUES(`brand`),"
           "`quantity`=VALUES(`quantity`),`category`=VALUES(`category`),"
           "`nutriscore`=VALUES(`nutriscore`),`labels`=VALUES(`labels`)";

    bool ok = execDirectMySQL(sql);
    if (ok)
        Logger::info("Sync", String("Product pushed to MySQL: ") + product.barcode);
    return ok;
}

bool SyncManager::fetchProductFromMySQL(const String &barcode, ProductInfo &out) {
    if (_ip.isEmpty() || WiFi.status() != WL_CONNECTED) return false;

    MySQLDirect db;
    if (!db.connect(_ip, 3306, _user, _pass, 3000)) return false;

    String sql =
        "SELECT `name`,`brand`,`quantity`,`category`,`nutriscore`,`labels` "
        "FROM `Lebensmittel_Scanner`.`product_cache` WHERE `barcode`='";
    sql += sqlEsc(barcode);
    sql += "' LIMIT 1";

    std::vector<std::vector<String>> rows;
    bool ok = db.queryRows(sql, rows, 1);
    db.close();

    if (!ok || rows.empty() || rows[0].size() < 6) return false;

    const auto &row = rows[0];
    out.barcode    = barcode;
    out.name       = row[0];
    out.brand      = row[1];
    out.quantity   = row[2];
    out.category   = row[3];
    out.nutriscore = row[4];
    labelsFromString(row[5], out.labels);

    return !out.name.isEmpty();
}

int SyncManager::pullProductsToCache(LittleFSManager &fs) {
    if (_ip.isEmpty() || WiFi.status() != WL_CONNECTED) return -1;

    MySQLDirect db;
    if (!db.connect(_ip, 3306, _user, _pass, 2000)) {
        Logger::warn("Sync", String("pullProductsToCache connect failed: ") + db.lastError());
        return -1;
    }

    std::vector<std::vector<String>> rows;
    bool ok = db.queryRows(
        "SELECT `barcode`,`name`,`brand`,`quantity`,`category`,`nutriscore`,`labels` "
        "FROM `Lebensmittel_Scanner`.`product_cache` "
        "ORDER BY `updated_at` DESC LIMIT 50",
        rows, 50);
    db.close();

    if (!ok) {
        Logger::warn("Sync", String("pullProductsToCache query failed: ") + db.lastError());
        return -1;
    }
    if (rows.empty()) {
        Logger::info("Sync", "pullProductsToCache: product_cache empty");
        return 0;
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &row : rows) {
        if (row.size() < 7) continue;
        JsonObject obj = arr.add<JsonObject>();
        obj["barcode"]    = row[0];
        obj["name"]       = row[1];
        obj["brand"]      = row[2];
        obj["quantity"]   = row[3];
        obj["category"]   = row[4];
        obj["nutriscore"] = row[5];
        JsonArray labels  = obj["labels"].to<JsonArray>();
        std::vector<String> lblVec;
        labelsFromString(row[6], lblVec);
        for (const String &l : lblVec) labels.add(l);
        obj["cachedAt"]   = (uint32_t)millis();
    }

    String serialized;
    serializeJson(doc, serialized);
    fs.writeFileAtomic("/off_cache.json", serialized);
    Logger::info("Sync", String("Boot sync: ") + (int)rows.size() + " products pulled from MySQL");
    return (int)rows.size();
}

// ---------- multi-device inventory sync -------------------------------------

std::vector<InventoryItem> SyncManager::pullInventory(const String &household, const String &ourDevice) {
    std::vector<InventoryItem> out;
    if (_ip.isEmpty() || WiFi.status() != WL_CONNECTED) return out;

    MySQLDirect db;
    if (!db.connect(_ip, 3306, _user, _pass, 2000)) {
        Logger::warn("Sync", String("pullInventory connect failed: ") + db.lastError());
        return out;
    }

    String sql =
        "SELECT `label_barcode`,`barcode`,`name`,`brand`,`category`,"
        "`expiry_date`,`added_date`,`quantity`,`location`,`device_name`"
        " FROM `Lebensmittel_Scanner`.`inventar`"
        " WHERE `household`='";
    sql += sqlEsc(household);
    sql += "' ORDER BY `synced_at` DESC LIMIT 300";

    std::vector<std::vector<String>> rows;
    bool ok = db.queryRows(sql, rows, 300);
    db.close();

    if (!ok) {
        Logger::warn("Sync", String("pullInventory query failed: ") + db.lastError());
        return out;
    }

    for (const auto &row : rows) {
        if (row.size() < 9) continue;
        InventoryItem item;
        item.labelBarcode = row[0];
        item.barcode      = row[1];
        item.name         = row[2];
        item.brand        = row[3];
        item.category     = row[4];
        item.expiryDate   = row[5];
        item.addedDate    = row[6];
        item.quantity     = row[7].toInt();
        item.location     = row[8];
        // device_name = row[9] (not stored in InventoryItem)
        out.push_back(item);
    }
    Logger::info("Sync", String("pullInventory: ") + out.size() + " items from MySQL");
    return out;
}

std::vector<String> SyncManager::pullRemovals(const String &household, const String &ourDevice) {
    std::vector<String> out;
    if (_ip.isEmpty() || WiFi.status() != WL_CONNECTED) return out;

    MySQLDirect db;
    if (!db.connect(_ip, 3306, _user, _pass, 2000)) {
        Logger::warn("Sync", String("pullRemovals connect failed: ") + db.lastError());
        return out;
    }

    String sql =
        "SELECT `label_barcode` FROM `Lebensmittel_Scanner`.`removed_items`"
        " WHERE `household`='";
    sql += sqlEsc(household);
    sql += "' AND `removed_by`!='";
    sql += sqlEsc(ourDevice);
    sql += "' ORDER BY `removed_at` DESC LIMIT 500";

    std::vector<std::vector<String>> rows;
    bool ok = db.queryRows(sql, rows, 500);
    db.close();

    if (!ok) {
        Logger::warn("Sync", String("pullRemovals query failed: ") + db.lastError());
        return out;
    }

    for (const auto &row : rows) {
        if (!row.empty() && !row[0].isEmpty()) out.push_back(row[0]);
    }
    Logger::info("Sync", String("pullRemovals: ") + out.size() + " tombstones from MySQL");
    return out;
}

std::vector<String> SyncManager::getHouseholds(const String &ownHousehold) {
    std::vector<String> out;
    if (_ip.isEmpty() || WiFi.status() != WL_CONNECTED) return out;
    MySQLDirect db;
    if (!db.connect(_ip, 3306, _user, _pass, 2000)) return out;
    String sql = "SELECT DISTINCT `household` FROM `Lebensmittel_Scanner`.`inventar`";
    if (!ownHousehold.isEmpty()) {
        sql += " WHERE `household`!='" + sqlEsc(ownHousehold) + "'";
    }
    sql += " ORDER BY `household` LIMIT 20";
    std::vector<std::vector<String>> rows;
    bool ok = db.queryRows(sql, rows, 20);
    db.close();
    if (!ok) return out;
    for (const auto &row : rows) {
        if (!row.empty() && !row[0].isEmpty()) out.push_back(row[0]);
    }
    Logger::info("Sync", String("getHouseholds: ") + out.size() + " others found");
    return out;
}
