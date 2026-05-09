#pragma once

#include <Arduino.h>

class BLEScanner {
public:
    void begin();
    void loop();
    bool readCode(String &code);

private:
    String pendingCode;
};
