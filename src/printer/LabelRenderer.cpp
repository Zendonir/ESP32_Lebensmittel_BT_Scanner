#include "LabelRenderer.h"
#include "config.h"
#include "../core/DeviceConfig.h"

LabelRenderer::LabelRenderer(EscPosPrinter &printer) : printer(printer) {}

bool LabelRenderer::printInventoryLabel(const InventoryItem &item) {
    if (!printer.isReady()) return false;

    const uint8_t W = LABEL_PAPER_CHARS;

    // Double reset: first ESC@ absorbs any incomplete command still in the
    // printer's input buffer; second ESC@ ensures a fully clean slate.
    printer.reset();
    printer.flush();
    delay(40);
    printer.reset();
    printer.setCodePage(16);          // restore WPC1252 after ESC @ reset
    printer.flush();
    delay(80);                        // let printer settle after reset

    printer.setLineSpacing(24);       // tighter line spacing (24/180 inch ≈ 3.4 mm)

    // Optional pre-feed (default 0 — previous label's nachlauf handles positioning)
    if (preFeedLines > 0) {
        printer.feed(preFeedLines);
        printer.flush();
        delay((uint32_t)preFeedLines * 60);
    }

    // ── Product name: bold, double-height (~5 mm), 2-space indent ───────────────
    String name = item.name.isEmpty() ? "Unbekanntes Produkt" : item.name;
    if (name.length() > (size_t)(W - 2)) name = name.substring(0, W - 2);
    printer.setDoubleHeight(true);
    printer.setBold(true);
    printer.println("  " + name);
    printer.setBold(false);
    printer.setDoubleHeight(false);

    // ── Field rows: 2-space indent, bold key, plain value ────────────────────────
    auto row = [&](const char *label, const String &value) {
        size_t labelLen = strlen(label);          // includes the 2-space prefix
        String val = value;
        if (val.length() > W - labelLen) val = val.substring(0, W - labelLen);
        printer.setBold(true);
        printer.writeText(label);
        printer.setBold(false);
        printer.println(val);
    };

    row("  Einlagerung: ", item.addedDate);
    row("  MHD:         ", item.expiryDate);
    String unitStr = item.unit.isEmpty() ? "St." : item.unit;
    row("  Menge:       ", String(item.quantity) + " " + unitStr);
    row("  Haushalt:    ", device_config.getHousehold());

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
