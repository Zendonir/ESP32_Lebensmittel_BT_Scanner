#include "BackupManager.h"
#include "AppFS.h"
#include "../core/Logger.h"
#include "../core/DeviceConfig.h"
#include <ArduinoJson.h>
#include <Preferences.h>

// ── helpers ──────────────────────────────────────────────────────────────────

static bool copyFile(fs::FS &src, fs::FS &dst, const char *srcPath, const char *dstPath) {
    File in = src.open(srcPath, "r");
    if (!in) return false;
    File out = dst.open(dstPath, "w");
    if (!out) { in.close(); return false; }
    uint8_t buf[256];
    while (int n = in.read(buf, sizeof(buf)))
        out.write(buf, n);
    in.close(); out.close();
    return true;
}

static void ensureDir(fs::FS &fs, const char *path) {
    if (!fs.exists(path)) fs.mkdir(path);
}

// Save DeviceConfig Preferences (household, location, ntfy, …) as JSON to SD
static void backupSettings(fs::FS &sd) {
    JsonDocument doc;
    doc["household"]     = device_config.getHousehold();
    doc["householdAbbr"] = device_config.getHouseholdAbbr();
    doc["deviceName"]    = device_config.getDeviceName();
    doc["activeLocation"]= device_config.getActiveLocation();
    doc["locColor"]      = device_config.getActiveLocationColor();
    doc["ntfyUrl"]       = device_config.getNtfyUrl();
    doc["ntfyTopic"]     = device_config.getNtfyTopic();
    doc["ntfyDays"]      = device_config.getNtfyDays();
    String path = String(BackupManager::BACKUP_DIR) + "/settings.json";
    File f = sd.open(path.c_str(), "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

// Restore DeviceConfig Preferences from SD backup
static void restoreSettings(fs::FS &sd) {
    String path = String(BackupManager::BACKUP_DIR) + "/settings.json";
    File f = sd.open(path.c_str(), "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    if (doc["household"].is<String>())      device_config.setHousehold(doc["household"].as<String>());
    if (doc["householdAbbr"].is<String>())  device_config.setHouseholdAbbr(doc["householdAbbr"].as<String>());
    if (doc["deviceName"].is<String>())     device_config.setDeviceName(doc["deviceName"].as<String>());
    if (doc["activeLocation"].is<String>()) device_config.setActiveLocation(doc["activeLocation"].as<String>());
    if (doc["locColor"].is<String>())       device_config.setActiveLocationColor(doc["locColor"].as<String>());
    if (doc["ntfyUrl"].is<String>())        device_config.setNtfyUrl(doc["ntfyUrl"].as<String>());
    if (doc["ntfyTopic"].is<String>())      device_config.setNtfyTopic(doc["ntfyTopic"].as<String>());
    if (doc["ntfyDays"].is<int>())          device_config.setNtfyDays(doc["ntfyDays"].as<int>());
}

// ── public API ────────────────────────────────────────────────────────────────

bool BackupManager::hasBackupOnSD() {
    if (!AppFS::sdAvailable()) return false;
    return AppFS::sdFs().exists(INFO_FILE);
}

bool BackupManager::internalIsEmpty() {
    fs::FS &internal = AppFS::internalFs();
    if (!internal.exists("/inventory.json")) return true;
    File f = internal.open("/inventory.json", "r");
    if (!f) return true;
    String content = f.readString();
    f.close();
    content.trim();
    return content.isEmpty() || content == "[]";
}

bool BackupManager::doBackup() {
    if (!AppFS::sdAvailable()) {
        Logger::warn("Backup", "SD nicht verfuegbar");
        return false;
    }
    fs::FS &src = AppFS::internalFs();
    fs::FS &sd  = AppFS::sdFs();
    ensureDir(sd, BACKUP_DIR);

    int ok = 0;
    for (int i = 0; BACKUP_FILES[i] != nullptr; i++) {
        String dst = String(BACKUP_DIR) + BACKUP_FILES[i];
        if (src.exists(BACKUP_FILES[i])) {
            if (copyFile(src, sd, BACKUP_FILES[i], dst.c_str())) ok++;
        }
    }

    // Backup device settings (Preferences)
    backupSettings(sd);

    // Write timestamp
    time_t now = time(nullptr);
    JsonDocument doc;
    doc["timestamp"] = (long)now;
    doc["files"] = ok;
    File inf = sd.open(INFO_FILE, "w");
    if (inf) { serializeJson(doc, inf); inf.close(); }

    // Persist last backup time in NVS
    Preferences p; p.begin("backup", false);
    p.putLong("lastTs", (long)now);
    p.end();

    Logger::info("Backup", String("Backup auf SD: ") + ok + " Dateien");
    return ok > 0;
}

bool BackupManager::doRestore() {
    if (!AppFS::sdAvailable()) return false;
    fs::FS &internal = AppFS::internalFs();
    fs::FS &sd       = AppFS::sdFs();

    int ok = 0;
    for (int i = 0; BACKUP_FILES[i] != nullptr; i++) {
        String src = String(BACKUP_DIR) + BACKUP_FILES[i];
        if (sd.exists(src.c_str())) {
            if (copyFile(sd, internal, src.c_str(), BACKUP_FILES[i])) ok++;
        }
    }
    // Restore device settings (Preferences)
    restoreSettings(sd);

    Logger::info("Backup", String("Restore von SD: ") + ok + " Dateien + Einstellungen");
    return ok > 0;
}

String BackupManager::backupDateString() {
    if (!AppFS::sdAvailable()) return "";
    File f = AppFS::sdFs().open(INFO_FILE, "r");
    if (!f) return "";
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return ""; }
    f.close();
    long ts = doc["timestamp"] | 0L;
    if (ts == 0) return "";
    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);
    char buf[20];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", tm);
    return String(buf);
}

void BackupManager::runDailyCheck() {
    if (!AppFS::sdAvailable()) return;
    Preferences p; p.begin("backup", true);
    long lastTs = p.getLong("lastTs", 0);
    p.end();
    long now = (long)time(nullptr);
    // time() returns 0 when NTP not synced yet — skip in that case
    if (now < 1000000L) return;
    if (now - lastTs >= 86400L) {
        doBackup();
    }
}
