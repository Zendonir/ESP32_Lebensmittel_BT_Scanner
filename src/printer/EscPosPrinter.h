#pragma once

#include <Arduino.h>
#include "config.h"

class EscPosPrinter {
public:
    void begin(uint32_t baud = UART_BAUD);
    bool isReady() const;
    void reset();
    void setBold(bool enabled);
    void setLarge(bool enabled);
    void println(const String &text);
    void feed(uint8_t lines = 3);
    void qrCode(const String &data);
    void flush();

private:
    HardwareSerial printerSerial = HardwareSerial(1);
    bool ready = false;
};
