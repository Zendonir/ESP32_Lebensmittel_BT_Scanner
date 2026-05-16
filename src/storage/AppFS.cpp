#include "AppFS.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include "../core/Logger.h"

// UserDataFS is intentionally file-scoped so callers never need the
// LittleFSFS type in their headers – they use AppFS::fs() instead.
static LittleFSFS UserDataFS;
static bool _sd = false;

bool AppFS::begin() {
    SD_MMC.setPins(11, 10, 9);   // CLK, CMD, D0
    if (SD_MMC.begin("/sdcard", true)) {   // 1-bit mode
        _sd = true;
        uint64_t mb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
        Logger::info("AppFS", String("SD card ") + mb + " MB – Datenspeicher auf SD");
        return true;
    }
    Logger::warn("AppFS", "Keine SD-Karte – Datenspeicher auf UserDataFS");
    _sd = false;
    return true;
}

bool AppFS::beginUserData() {
    if (!UserDataFS.begin(false, "/userdata", 10, "userdata")) {
        Logger::warn("LittleFS", "Userdata partition mount failed – formatting once");
        if (!UserDataFS.begin(true, "/userdata", 10, "userdata")) {
            Logger::error("LittleFS", "Userdata partition format failed");
            return false;
        }
    }
    Logger::info("LittleFS", String("Userdata – ") + UserDataFS.usedBytes() + "/" + UserDataFS.totalBytes() + " B");
    return true;
}

bool AppFS::usingSD() { return _sd; }

fs::FS& AppFS::fs() {
    return _sd ? (fs::FS&)SD_MMC : (fs::FS&)UserDataFS;
}

fs::FS& AppFS::webFs() {
    return (fs::FS&)LittleFS;
}

void AppFS::formatUserData() {
    UserDataFS.format();
}

size_t AppFS::userDataUsedBytes() {
    return _sd ? SD_MMC.usedBytes() : UserDataFS.usedBytes();
}

size_t AppFS::userDataTotalBytes() {
    return _sd ? SD_MMC.totalBytes() : UserDataFS.totalBytes();
}
