#pragma once

#include <Arduino.h>
#include "config.h"

class EscPosPrinter {
public:
    void begin(uint32_t baud = UART_BAUD);
    bool isReady() const;
    void reset();
    void setLineSpacing(uint8_t n);  // ESC 3 n — n/180 inch per line; 0=default
    void setBold(bool enabled);
    void setLarge(bool enabled);
    void setUnderline(bool enabled);
    void setAlign(uint8_t align);  // 0=left  1=center  2=right
    size_t println(const String &text);
    size_t printlnCrLf(const String &text);
    // Print a two-column row; right value is underlined; total width = paperChars
    size_t printLabelRow(const String &label, const String &value, uint8_t paperChars);
    void feed(uint8_t lines = 3);      // ESC d — forward feed n lines
    void feedDots(uint16_t dots);      // ESC J — forward feed n dots (chunked)
    void backFeed(uint8_t lines);
    // Raw reverse feed: cmd=0x6A(ESC j), 0x4B(ESC K), 0x65(ESC e)
    // chunkSize=0 → single command, >0 → split into chunks of chunkSize dots
    // delayMs per chunk (0=no delay), doFlush=flush after each chunk
    void rawBackfeed(uint8_t cmd, uint16_t dots, uint8_t chunkSize, uint16_t delayMs, bool doFlush);
    void qrCode(const String &data);
    // Code 128 barcode (GS k); call barcodeHeight/Width before barcode128 if needed
    void barcodeHeight(uint8_t dots);     // GS h  (default 162)
    void barcodeWidth(uint8_t mult);      // GS w  (2-6, default 3)
    void barcodeHRI(uint8_t pos);         // GS H  (0=none 1=above 2=below)
    void barcode128(const String &data);  // GS k 73 n {B data
    size_t writeBytes(const uint8_t *data, size_t length);
    size_t writeText(const String &text);
    void flush();

private:
    HardwareSerial printerSerial = HardwareSerial(1);
    bool ready = false;
};
