#pragma once

#include <Arduino.h>

class TimeManager {
public:
    void begin();
    String today() const;
    String addDays(int days) const;
};
