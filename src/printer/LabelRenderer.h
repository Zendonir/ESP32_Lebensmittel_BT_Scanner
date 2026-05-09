#pragma once

#include "EscPosPrinter.h"
#include "../models/InventoryItem.h"

class LabelRenderer {
public:
    explicit LabelRenderer(EscPosPrinter &printer);
    bool printInventoryLabel(const InventoryItem &item);

private:
    EscPosPrinter &printer;
};
