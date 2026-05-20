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
    TouchPoint p = current_point;

    if (p.pressed) {
        // FT6336 reports portrait panel coordinates (X=0..319, Y=0..479).
        // TFT_eSPI rotation=1 (MX|MV) maps portrait → landscape:
        //   landscape_X = portrait_Y
        //   landscape_Y = (portrait_WIDTH - 1) - portrait_X
        uint16_t rawX = constrain(p.x, 0, DISPLAY_WIDTH  - 1);
        uint16_t rawY = constrain(p.y, 0, DISPLAY_HEIGHT - 1);

        lastRawX = rawX;
        lastRawY = rawY;

        p.x = rawY;
        p.y = (DISPLAY_WIDTH - 1) - rawX;

        p.x = constrain(p.x, 0, DISPLAY_LANDSCAPE_WIDTH  - 1);
        p.y = constrain(p.y, 0, DISPLAY_LANDSCAPE_HEIGHT - 1);

        lastCalX = p.x;
        lastCalY = p.y;
    }

    return p;
}

bool Touch::isPressed() {
    return current_point.pressed;
}
