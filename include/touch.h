#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

struct TouchPoint {
    uint16_t x;
    uint16_t y;
    bool pressed;
};

class Touch {
public:
    Touch();
    void init(TwoWire *wire);
    TouchPoint read();
    bool isPressed();

    // Last raw (pre-transformation) and calibrated coordinates for debugging
    uint16_t lastRawX  = 0;
    uint16_t lastRawY  = 0;
    uint16_t lastCalX  = 0;
    uint16_t lastCalY  = 0;

private:
    TwoWire *i2c;
    TouchPoint current_point;

    void readRaw();
    TouchPoint getCalibrated();
};

extern Touch touch_obj;

#endif
