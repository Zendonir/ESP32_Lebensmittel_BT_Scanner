#pragma once

#include <Arduino.h>

struct CategoryDef {
    String name;
    uint16_t bgColor = 0x0000;
    uint16_t textColor = 0xFFFF;
};
