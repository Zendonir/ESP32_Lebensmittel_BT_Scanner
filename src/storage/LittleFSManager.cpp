#include "LittleFSManager.h"
#include "../core/Logger.h"
#include <LittleFS.h>

bool LittleFSManager::begin() {
    if (LittleFS.begin(false)) return true;
    Logger::warn("LittleFS", "Mount failed; formatting filesystem");
    return LittleFS.begin(true);
}

bool LittleFSManager::exists(const char *path) const {
    return LittleFS.exists(path);
}

bool LittleFSManager::readFile(const char *path, String &out) const {
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    out = file.readString();
    file.close();
    return true;
}

bool LittleFSManager::writeFileAtomic(const char *path, const String &content) const {
    String tmpPath = String(path) + ".tmp";
    File file = LittleFS.open(tmpPath, "w");
    if (!file) return false;
    size_t written = file.print(content);
    file.close();
    if (written != content.length()) {
        LittleFS.remove(tmpPath);
        return false;
    }
    LittleFS.remove(path);
    return LittleFS.rename(tmpPath, path);
}

bool LittleFSManager::ensureJsonFile(const char *path, const char *defaultJson) const {
    if (exists(path)) return true;
    return writeFileAtomic(path, defaultJson);
}
