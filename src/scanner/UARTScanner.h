#pragma once

#include <Arduino.h>
#include "config.h"

class UARTScanner {
public:
    void begin(uint32_t baud = UART_BAUD);
    bool readCode(String &code);

private:
    HardwareSerial scannerSerial = HardwareSerial(2);
};
