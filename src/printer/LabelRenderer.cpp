#include "LabelRenderer.h"

LabelRenderer::LabelRenderer(EscPosPrinter &printer) : printer(printer) {}

bool LabelRenderer::printInventoryLabel(const InventoryItem &item) {
    if (!printer.isReady()) return false;
    printer.reset();
    printer.setBold(true);
    printer.setLarge(true);
    printer.println(item.name.isEmpty() ? "Lebensmittel" : item.name);
    printer.setLarge(false);
    printer.setBold(false);
    printer.println("Eingelagert: " + item.addedDate);
    printer.println("MHD: " + item.expiryDate);
    printer.println("Menge: " + String(item.quantity) + " Stueck");
    printer.println("Auslagern: " + item.labelBarcode);
    printer.qrCode(item.labelBarcode);
    printer.feed(4);
    return true;
}
