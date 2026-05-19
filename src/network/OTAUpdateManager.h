#pragma once

#include <Arduino.h>

class OTAUpdateManager {
public:
    void begin();
    bool updateFromUrl(const String &url);
};
