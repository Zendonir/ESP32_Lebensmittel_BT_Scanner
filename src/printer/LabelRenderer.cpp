#include "LabelRenderer.h"
#include "config.h"

LabelRenderer::LabelRenderer(EscPosPrinter &printer) : printer(printer) {}

static String trunc(const String &s, size_t maxLen) {
    return s.length() <= maxLen ? s : s.substring(0, maxLen);
}

static String rule(uint8_t n, char c = '=') {
    String s;
    s.reserve(n);
    for (uint8_t i = 0; i < n; i++) s += c;
    return s;
}

bool LabelRenderer::printInventoryLabel(const InventoryItem &item) {
    if (!printer.isReady()) return false;

    const uint8_t W = LABEL_PAPER_CHARS;

    printer.reset();

    // ── Product name (centered, large, bold) ─────────────────────────────────
    printer.println(rule(W));
    printer.setLarge(true);
    printer.setBold(true);
    printer.setAlign(1);
    printer.println(trunc(item.name.isEmpty() ? "Lebensmittel" : item.name, W * 2));
    printer.setAlign(0);
    printer.setBold(false);
    printer.setLarge(false);
    printer.println(rule(W, '-'));

    // ── Field rows: bold label + underlined value ─────────────────────────────
    printer.printLabelRow("Einlagerung: ", item.addedDate,                W);
    printer.printLabelRow("MHD:         ", item.expiryDate,               W);
    printer.printLabelRow("Menge:       ", String(item.quantity) + " St.",W);
    printer.printLabelRow("Haushalt:    ", trunc(LABEL_HOUSEHOLD, W),     W);

    // ── Code 128 barcode (centered, below text) ───────────────────────────────
    printer.println(rule(W, '-'));
    printer.barcodeHeight(60);
    printer.barcodeWidth(2);
    printer.barcodeHRI(2);   // human-readable text below barcode
    printer.setAlign(1);
    String barcodeData = item.labelBarcode.isEmpty() ? item.barcode : item.labelBarcode;
    printer.barcode128(barcodeData);
    printer.setAlign(0);

    printer.feed(4);
    printer.flush();
    return true;
}
