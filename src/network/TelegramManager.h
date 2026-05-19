#pragma once

#include <Arduino.h>

class TelegramManager {
public:
    void begin();
    bool notify(const String &message);
};
