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
    void setUnderline(bool enabled);
    void setAlign(uint8_t align);  // 0=left  1=center  2=right
    size_t println(const String &text);
    size_t printlnCrLf(const String &text);
    // Print a two-column row; right value is underlined; total width = paperChars
    size_t printLabelRow(const String &label, const String &value, uint8_t paperChars);
    void feed(uint8_t lines = 3);
    void qrCode(const String &data);
    size_t writeBytes(const uint8_t *data, size_t length);
    size_t writeText(const String &text);
    void flush();

private:
    HardwareSerial printerSerial = HardwareSerial(1);
    bool ready = false;
};
