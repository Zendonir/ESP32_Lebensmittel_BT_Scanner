#pragma once

#include <Arduino.h>
#include "EscPosPrinter.h"
#include "LabelRenderer.h"

class PrinterManager {
public:
    PrinterManager();
    void begin();
    void configure(uint32_t baud);
    bool isReady() const;
    uint32_t baud() const;
    void feed(uint8_t lines);
    void backFeed(uint8_t lines);
    void setBackfeedConfig(bool enabled, uint8_t lines);
    // Raw test: cmd=0x6A/0x4B/0x65, dots, chunkSize(0=single), delayMs, flush
    bool testBackfeed(uint8_t cmd, uint16_t dots, uint8_t chunkSize, uint16_t delayMs, bool doFlush);
    bool printLabel(const InventoryItem &item);
    size_t printTestPage(bool includeQr = false);
    size_t printPlainTest();
    size_t printBaudProbe();

private:
    EscPosPrinter printer;
    LabelRenderer renderer;
    uint32_t currentBaud     = 0;
    bool     backfeedEnabled = false;
    uint8_t  backfeedLines   = 3;
};
