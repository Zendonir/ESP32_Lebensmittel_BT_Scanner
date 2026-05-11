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
    bool printLabel(const InventoryItem &item);
    bool printTestPage(bool includeQr = false);

private:
    EscPosPrinter printer;
    LabelRenderer renderer;
    uint32_t currentBaud = 0;
};
