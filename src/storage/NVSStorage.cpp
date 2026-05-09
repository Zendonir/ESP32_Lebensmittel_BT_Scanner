#include "NVSStorage.h"

bool NVSStorage::begin(const char *nameSpace, bool readOnly) {
    opened = prefs.begin(nameSpace, readOnly);
    return opened;
}

void NVSStorage::end() {
    if (opened) prefs.end();
    opened = false;
}

uint32_t NVSStorage::getUInt(const char *key, uint32_t defaultValue) {
    return opened ? prefs.getUInt(key, defaultValue) : defaultValue;
}

void NVSStorage::putUInt(const char *key, uint32_t value) {
    if (opened) prefs.putUInt(key, value);
}

String NVSStorage::getString(const char *key, const String &defaultValue) {
    return opened ? prefs.getString(key, defaultValue) : defaultValue;
}

void NVSStorage::putString(const char *key, const String &value) {
    if (opened) prefs.putString(key, value);
}
