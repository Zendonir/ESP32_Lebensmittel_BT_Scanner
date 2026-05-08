#include "touch.h"

Touch touch_obj;

Touch::Touch() : i2c(nullptr) {
    current_point = {0, 0, false};
}

void Touch::init(TwoWire *wire) {
    i2c = wire;
    if (TOUCH_INT >= 0) {
        pinMode(TOUCH_INT, INPUT);
    }
}

TouchPoint Touch::read() {
    readRaw();
    return getCalibrated();
}

void Touch::readRaw() {
    if (i2c == nullptr) {
        current_point.pressed = false;
        return;
    }

    i2c->beginTransmission(TOUCH_ADDR);
    i2c->write(0x00);
    if (i2c->endTransmission(false) != 0) {
        current_point.pressed = false;
        return;
    }

    i2c->requestFrom(TOUCH_ADDR, 16, true);
    if (i2c->available() < 16) {
        current_point.pressed = false;
        return;
    }

    uint8_t data[16];
    for (int i = 0; i < 16; i++) {
        data[i] = i2c->read();
    }

    uint8_t num_touches = data[2] & 0x0F;
    if (num_touches == 0) {
        current_point.pressed = false;
        return;
    }

    // First touch point
    current_point.x = ((data[3] & 0x0F) << 8) | data[4];
    current_point.y = ((data[5] & 0x0F) << 8) | data[6];
    current_point.pressed = true;
}

TouchPoint Touch::getCalibrated() {
    // Swap coordinates if needed (portrait mode)
    TouchPoint p = current_point;

    // Calibration: adjust for display resolution
    if (p.pressed) {
        // Example calibration values - adjust based on actual touch points
        p.x = constrain(p.x, 0, 480);
        p.y = constrain(p.y, 0, 320);

        // Swap X and Y for proper orientation
        uint16_t temp = p.x;
        p.x = p.y;
        p.y = temp;
    }

    return p;
}

bool Touch::isPressed() {
    return current_point.pressed;
}
