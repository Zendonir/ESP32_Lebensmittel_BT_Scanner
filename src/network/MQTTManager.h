#pragma once

#include <Arduino.h>

class MQTTManager {
public:
    void begin();
    void loop();
    bool publishEvent(const String &topicSuffix, const String &payload);
};
