#include "LabelRenderer.h"
#include "config.h"

LabelRenderer::LabelRenderer(EscPosPrinter &printer) : printer(printer) {}

bool LabelRenderer::printInventoryLabel(const InventoryItem &item) {
    if (!printer.isReady()) return false;

    const uint8_t W = LABEL_PAPER_CHARS;

    printer.feed(preFeedLines);
    printer.flush();
    delay(300);

    printer.reset();

    // ── Product name: always bold normal size, left-aligned, truncated ──────────
    String name = item.name.isEmpty() ? "Unbekanntes Produkt" : item.name;
    if (name.length() > W) name = name.substring(0, W);
    printer.setBold(true);
    printer.println(name);
    printer.setBold(false);

    // ── Field rows: bold label, plain value, no underline, no separator ───────
    auto row = [&](const char *label, const String &value) {
        size_t labelLen = strlen(label);
        String val = value;
        if (val.length() > W - labelLen) val = val.substring(0, W - labelLen);
        printer.setBold(true);
        printer.writeText(label);
        printer.setBold(false);
        printer.println(val);
    };

    row("Einlagerung: ", item.addedDate);
    row("MHD:         ", item.expiryDate);
    row("Menge:       ", String(item.quantity) + " St.");
    row("Haushalt:    ", String(LABEL_HOUSEHOLD));

    // ── Code 128 barcode, no HRI text ────────────────────────────────────────
    printer.barcodeHeight(60);
    printer.barcodeWidth(2);
    printer.barcodeHRI(0);
    printer.setAlign(1);
    String bc = item.labelBarcode.isEmpty() ? item.barcode : item.labelBarcode;
    printer.barcode128(bc);
    printer.setAlign(0);

    printer.feedDots(postFeedDots);
    printer.flush();
    return true;
}
