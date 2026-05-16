#include "AppFS.h"
#include <SD_MMC.h>
#include "../core/Logger.h"

LittleFSFS UserDataFS;

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

bool AppFS::usingSD() { return _sd; }

fs::FS& AppFS::fs() {
    return _sd ? (fs::FS&)SD_MMC : (fs::FS&)UserDataFS;
}

fs::FS& AppFS::webFs() {
    return (fs::FS&)LittleFS;
}
