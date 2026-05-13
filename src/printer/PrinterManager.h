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
    void feed(uint8_t lines);   // advance paper (manual positioning)
    bool printLabel(const InventoryItem &item);
    size_t printTestPage(bool includeQr = false);
    size_t printPlainTest();
    size_t printBaudProbe();

private:
    EscPosPrinter printer;
    LabelRenderer renderer;
    uint32_t currentBaud = 0;
};
