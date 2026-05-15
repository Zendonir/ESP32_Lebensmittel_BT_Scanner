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
    void setPostFeed(uint16_t lines);
    bool printLabel(const InventoryItem &item);
    void printManual(const String &text, bool center = false);
    void printManualLabel(const String &text, bool center = false);
    size_t printTestPage(bool includeQr = false);
    size_t printPlainTest();
    size_t printBaudProbe();

private:
    EscPosPrinter printer;
    LabelRenderer renderer;
    uint32_t currentBaud  = 0;
    uint16_t postFeedDots = 86;
};
