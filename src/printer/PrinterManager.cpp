#include "PrinterManager.h"

PrinterManager::PrinterManager() : renderer(printer) {}

void PrinterManager::begin() {
    printer.begin();
}

bool PrinterManager::printLabel(const InventoryItem &item) {
    return renderer.printInventoryLabel(item);
}
