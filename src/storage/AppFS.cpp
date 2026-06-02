#include "AppFS.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include "../core/Logger.h"

// Use decltype so we match whatever class LittleFS is in this framework version,
// avoiding a hard-coded 'LittleFSFS' that may not exist.
static decltype(LittleFS) UserDataFS;
static bool _sd = false;

bool AppFS::begin() {
    SD_MMC.end();  // sauber demounten – nach OTA-Soft-Reset kann der Treiber halb-init sein
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

bool AppFS::usingSD()      { return _sd; }
bool AppFS::sdAvailable()  { return _sd; }

// Primary storage is always internal — SD is backup only
fs::FS& AppFS::fs()         { return (fs::FS&)UserDataFS; }
fs::FS& AppFS::internalFs() { return (fs::FS&)UserDataFS; }
fs::FS& AppFS::sdFs()       { return (fs::FS&)SD_MMC; }

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
