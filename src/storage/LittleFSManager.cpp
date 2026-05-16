#include "LittleFSManager.h"
#include "../core/Logger.h"
#include <LittleFS.h>
#include "AppFS.h"

bool LittleFSManager::begin() {
    // Try to mount without formatting first so uploaded web files are preserved.
    if (LittleFS.begin(false)) {
        Logger::info("LittleFS", String("Mounted – ") + LittleFS.usedBytes() + "/" + LittleFS.totalBytes() + " bytes used");
        AppFS::begin();
        return true;
    }
    // Only format if the partition exists but has never been initialised
    // (i.e. first boot after flashing a fresh chip, no uploadfs done yet).
    Logger::warn("LittleFS", "Mount failed – partition may be uninitialised, formatting once");
    if (!LittleFS.begin(true)) {
        Logger::error("LittleFS", "Format + mount failed – check partition table");
        return false;
    }
    Logger::warn("LittleFS", "Formatted OK – run 'Upload Filesystem Image' to install web files");
    AppFS::begin();
    return true;
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
