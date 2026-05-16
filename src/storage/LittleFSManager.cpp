#include "LittleFSManager.h"
#include "../core/Logger.h"
#include <LittleFS.h>
#include "AppFS.h"

bool LittleFSManager::begin() {
    // Mount web-files partition (label "spiffs" – this is what uploadfs writes to).
    if (!LittleFS.begin(false, "/webfiles", 10, "spiffs")) {
        Logger::warn("LittleFS", "Web partition mount failed – formatting once");
        if (!LittleFS.begin(true, "/webfiles", 10, "spiffs"))
            Logger::error("LittleFS", "Web partition format failed");
    }
    Logger::info("LittleFS", String("Web partition – ") + LittleFS.usedBytes() + "/" + LittleFS.totalBytes() + " B");

    // Mount user-data partition (label "userdata" – never touched by uploadfs).
    AppFS::beginUserData();

    AppFS::begin();
    return true;
}

bool LittleFSManager::exists(const char *path) const {
    return AppFS::fs().exists(path);
}

bool LittleFSManager::readFile(const char *path, String &out) const {
    File file = AppFS::fs().open(path, "r");
    if (!file) return false;
    out = file.readString();
    file.close();
    return true;
}

bool LittleFSManager::writeFileAtomic(const char *path, const String &content) const {
    String tmpPath = String(path) + ".tmp";
    File file = AppFS::fs().open(tmpPath, "w");
    if (!file) return false;
    size_t written = file.print(content);
    file.close();
    if (written != content.length()) {
        AppFS::fs().remove(tmpPath);
        return false;
    }
    AppFS::fs().remove(path);
    return AppFS::fs().rename(tmpPath, path);
}

bool LittleFSManager::ensureJsonFile(const char *path, const char *defaultJson) const {
    if (exists(path)) return true;
    return writeFileAtomic(path, defaultJson);
}
