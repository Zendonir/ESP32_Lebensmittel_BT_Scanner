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
    size_t println(const String &text);
    size_t printlnCrLf(const String &text);
    void feed(uint8_t lines = 3);
    void qrCode(const String &data);
    size_t writeBytes(const uint8_t *data, size_t length);
    size_t writeText(const String &text);
    void flush();

private:
    HardwareSerial printerSerial = HardwareSerial(1);
    bool ready = false;
};
