#pragma once

#include <Arduino.h>

class LittleFSManager {
public:
    bool begin();
    bool exists(const char *path) const;
    bool readFile(const char *path, String &out) const;
    bool writeFileAtomic(const char *path, const String &content) const;
    bool ensureJsonFile(const char *path, const char *defaultJson) const;
};
