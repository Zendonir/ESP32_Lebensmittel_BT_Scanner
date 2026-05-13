#include "LabelRenderer.h"
#include "BitmapLabel.h"

LabelRenderer::LabelRenderer(EscPosPrinter &printer) : printer(printer) {}

bool LabelRenderer::printInventoryLabel(const InventoryItem &item) {
    BitmapLabel bmp(printer);
    return bmp.printInventoryLabel(item);
}
