#pragma once
#include <FS.h>

namespace AppFS {
    bool begin();             // try SD_MMC (for backup only), always uses internal as primary
    bool beginUserData();     // mount the userdata LittleFS partition
    bool usingSD();           // kept for legacy display
    bool sdAvailable();       // SD is mounted and accessible
    fs::FS& fs();             // user data: always internal userdata partition
    fs::FS& internalFs();     // explicit internal FS (same as fs())
    fs::FS& sdFs();           // SD filesystem — only call if sdAvailable()
    fs::FS& webFs();          // web static files: spiffs partition (LittleFS)
    void formatUserData();    // erase only the userdata partition (factory-reset)
    size_t userDataUsedBytes();
    size_t userDataTotalBytes();
}
