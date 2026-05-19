#pragma once

#include <Arduino.h>
#include "../storage/NVSStorage.h"

class LabelCounter {
public:
    bool begin();
    String nextLabel();

private:
    NVSStorage nvs;
};
