#pragma once
#include <FS.h>
#include <LittleFS.h>

// Second LittleFS instance mounted on the "userdata" partition.
// Web-file serving uses the global LittleFS (mounted on "spiffs" partition).
// User data uses UserDataFS so uploadfs never overwrites it.
extern LittleFSFS UserDataFS;

namespace AppFS {
    bool begin();
    bool usingSD();
    fs::FS& fs();      // user data: SD_MMC if mounted, else UserDataFS
    fs::FS& webFs();   // web static files: always LittleFS (spiffs partition)
}
