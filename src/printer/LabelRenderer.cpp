#include "LabelRenderer.h"
#include "config.h"

LabelRenderer::LabelRenderer(EscPosPrinter &printer) : printer(printer) {}

// Truncate string to at most maxLen characters.
static String trunc(const String &s, size_t maxLen) {
    return s.length() <= maxLen ? s : s.substring(0, maxLen);
}

// Build a horizontal rule of exactly `n` '=' characters.
static String rule(uint8_t n, char c = '=') {
    String s;
    s.reserve(n);
    for (uint8_t i = 0; i < n; i++) s += c;
    return s;
}

bool LabelRenderer::printInventoryLabel(const InventoryItem &item) {
    if (!printer.isReady()) return false;

    const uint8_t W = LABEL_PAPER_CHARS;   // chars per line

    printer.reset();

    // ── Top rule ─────────────────────────────────────────────
    printer.println(rule(W));

    // ── Product name (centered, large, bold) ─────────────────
    printer.setLarge(true);
    printer.setBold(true);
    printer.setAlign(1);  // center
    String name = trunc(item.name.isEmpty() ? "Lebensmittel" : item.name,
                        W * 2);  // large font is ~half as many chars per line
    printer.println(name);
    printer.setAlign(0);
    printer.setBold(false);
    printer.setLarge(false);

    // ── Separator ────────────────────────────────────────────
    printer.println(rule(W, '-'));

    // ── Field rows: bold label  underlined value ─────────────
    // Column widths: label col fixed to 13 chars, value fills the rest
    printer.printLabelRow("Einlagerung: ", item.addedDate,   W);
    printer.printLabelRow("MHD:         ", item.expiryDate,  W);

    String qty = String(item.quantity) + " St.";
    printer.printLabelRow("Menge:       ", qty,              W);

    String household = trunc(LABEL_HOUSEHOLD, W);
    printer.printLabelRow("Haushalt:    ", household,        W);

    // ── Bottom rule + centered QR code ───────────────────────
    printer.println(rule(W, '-'));
    printer.setAlign(1);
    printer.qrCode(item.labelBarcode);
    printer.setAlign(0);

    printer.feed(4);
    printer.flush();
    return true;
}
