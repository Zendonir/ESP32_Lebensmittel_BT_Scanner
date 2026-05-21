#pragma once

#include <Arduino.h>
#include "../storage/NVSStorage.h"

class LabelCounter {
public:
    bool begin();
    String nextLabel();

    // Roll tracking
    void     newRoll(uint32_t size);   // start a new roll of `size` labels
    int      getRemaining();           // -1 = not configured; ≥0 = labels left on roll
    uint32_t getRollSize();            // 0 = never configured

private:
    NVSStorage nvs;
};
