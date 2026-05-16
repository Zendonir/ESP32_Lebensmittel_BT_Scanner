#pragma once
#include <FS.h>

namespace AppFS {
    bool begin();           // try SD_MMC, fall back silently
    bool beginUserData();   // mount the userdata LittleFS partition
    bool usingSD();
    fs::FS& fs();           // user data: SD_MMC if mounted, else userdata partition
    fs::FS& webFs();        // web static files: spiffs partition (LittleFS)
    void formatUserData();  // erase only the userdata partition (factory-reset)
    size_t userDataUsedBytes();
    size_t userDataTotalBytes();
}
