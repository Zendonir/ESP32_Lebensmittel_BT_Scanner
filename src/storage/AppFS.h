#pragma once
#include <FS.h>

namespace AppFS {
    bool begin();        // tries SD_MMC, then falls back silently
    bool usingSD();
    fs::FS& fs();
}
