#pragma once

#include <Arduino.h>
#include <Preferences.h>

class NVSStorage {
public:
    bool begin(const char *nameSpace, bool readOnly = false);
    void end();
    uint32_t getUInt(const char *key, uint32_t defaultValue = 0);
    void putUInt(const char *key, uint32_t value);
    String getString(const char *key, const String &defaultValue = "");
    void putString(const char *key, const String &value);

private:
    Preferences prefs;
    bool opened = false;
};
