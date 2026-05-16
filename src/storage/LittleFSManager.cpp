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
    if (!UserDataFS.begin(false, "/userdata", 10, "userdata")) {
        Logger::warn("LittleFS", "Userdata partition mount failed – formatting once");
        if (!UserDataFS.begin(true, "/userdata", 10, "userdata")) {
            Logger::error("LittleFS", "Userdata partition format failed");
            return false;
        }
    }
    Logger::info("LittleFS", String("Userdata partition – ") + UserDataFS.usedBytes() + "/" + UserDataFS.totalBytes() + " B");

    AppFS::begin();
    return true;
}

bool LittleFSManager::exists(const char *path) const {
    return UserDataFS.exists(path);
}

bool LittleFSManager::readFile(const char *path, String &out) const {
    File file = UserDataFS.open(path, "r");
    if (!file) return false;
    out = file.readString();
    file.close();
    return true;
}

bool LittleFSManager::writeFileAtomic(const char *path, const String &content) const {
    String tmpPath = String(path) + ".tmp";
    File file = UserDataFS.open(tmpPath, "w");
    if (!file) return false;
    size_t written = file.print(content);
    file.close();
    if (written != content.length()) {
        UserDataFS.remove(tmpPath);
        return false;
    }
    UserDataFS.remove(path);
    return UserDataFS.rename(tmpPath, path);
}

bool LittleFSManager::ensureJsonFile(const char *path, const char *defaultJson) const {
    if (exists(path)) return true;
    return writeFileAtomic(path, defaultJson);
}
