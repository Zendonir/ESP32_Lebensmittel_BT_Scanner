#pragma once

#include "EscPosPrinter.h"
#include "LabelRenderer.h"

class PrinterManager {
public:
    PrinterManager();
    void begin();
    bool printLabel(const InventoryItem &item);

private:
    EscPosPrinter printer;
    LabelRenderer renderer;
};
