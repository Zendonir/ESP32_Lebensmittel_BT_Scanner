#include "BitmapLabel.h"
#include "config.h"
#include <qrcode.h>

// ── Font ────────────────────────────────────────────────────────────────────
// Adafruit-style 5×7 font, column-by-column, 5 bytes per glyph.
// Each byte = one column; bit 0 = top row, bit 6 = bottom row, bit 7 unused.
// Covers ASCII 0x20 (space) through 0x7E (~).  95 glyphs × 5 bytes = 475 B.
static const uint8_t FONT5x7[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00, // ' '
  0x00,0x00,0x5F,0x00,0x00, // !
  0x00,0x07,0x00,0x07,0x00, // "
  0x14,0x7F,0x14,0x7F,0x14, // #
  0x24,0x2A,0x7F,0x2A,0x12, // $
  0x23,0x13,0x08,0x64,0x62, // %
  0x36,0x49,0x55,0x22,0x50, // &
  0x00,0x05,0x03,0x00,0x00, // '
  0x00,0x1C,0x22,0x41,0x00, // (
  0x00,0x41,0x22,0x1C,0x00, // )
  0x08,0x2A,0x1C,0x2A,0x08, // *
  0x08,0x08,0x3E,0x08,0x08, // +
  0x00,0x50,0x30,0x00,0x00, // ,
  0x08,0x08,0x08,0x08,0x08, // -
  0x00,0x60,0x60,0x00,0x00, // .
  0x20,0x10,0x08,0x04,0x02, // /
  0x3E,0x51,0x49,0x45,0x3E, // 0
  0x00,0x42,0x7F,0x40,0x00, // 1
  0x42,0x61,0x51,0x49,0x46, // 2
  0x21,0x41,0x45,0x4B,0x31, // 3
  0x18,0x14,0x12,0x7F,0x10, // 4
  0x27,0x45,0x45,0x45,0x39, // 5
  0x3C,0x4A,0x49,0x49,0x30, // 6
  0x01,0x71,0x09,0x05,0x03, // 7
  0x36,0x49,0x49,0x49,0x36, // 8
  0x06,0x49,0x49,0x29,0x1E, // 9
  0x00,0x36,0x36,0x00,0x00, // :
  0x00,0x56,0x36,0x00,0x00, // ;
  0x08,0x14,0x22,0x41,0x00, // <
  0x14,0x14,0x14,0x14,0x14, // =
  0x00,0x41,0x22,0x14,0x08, // >
  0x02,0x01,0x51,0x09,0x06, // ?
  0x32,0x49,0x79,0x41,0x3E, // @
  0x7E,0x11,0x11,0x11,0x7E, // A
  0x7F,0x49,0x49,0x49,0x36, // B
  0x3E,0x41,0x41,0x41,0x22, // C
  0x7F,0x41,0x41,0x22,0x1C, // D
  0x7F,0x49,0x49,0x49,0x41, // E
  0x7F,0x09,0x09,0x09,0x01, // F
  0x3E,0x41,0x41,0x49,0x7A, // G
  0x7F,0x08,0x08,0x08,0x7F, // H
  0x00,0x41,0x7F,0x41,0x00, // I
  0x20,0x40,0x41,0x3F,0x01, // J
  0x7F,0x08,0x14,0x22,0x41, // K
  0x7F,0x40,0x40,0x40,0x40, // L
  0x7F,0x02,0x04,0x02,0x7F, // M
  0x7F,0x04,0x08,0x10,0x7F, // N
  0x3E,0x41,0x41,0x41,0x3E, // O
  0x7F,0x09,0x09,0x09,0x06, // P
  0x3E,0x41,0x51,0x21,0x5E, // Q
  0x7F,0x09,0x19,0x29,0x46, // R
  0x46,0x49,0x49,0x49,0x31, // S
  0x01,0x01,0x7F,0x01,0x01, // T
  0x3F,0x40,0x40,0x40,0x3F, // U
  0x1F,0x20,0x40,0x20,0x1F, // V
  0x3F,0x40,0x38,0x40,0x3F, // W
  0x63,0x14,0x08,0x14,0x63, // X
  0x07,0x08,0x70,0x08,0x07, // Y
  0x61,0x51,0x49,0x45,0x43, // Z
  0x00,0x7F,0x41,0x41,0x00, // [
  0x02,0x04,0x08,0x10,0x20, // backslash
  0x00,0x41,0x41,0x7F,0x00, // ]
  0x04,0x02,0x01,0x02,0x04, // ^
  0x40,0x40,0x40,0x40,0x40, // _
  0x00,0x01,0x02,0x04,0x00, // `
  0x20,0x54,0x54,0x54,0x78, // a
  0x7F,0x48,0x44,0x44,0x38, // b
  0x38,0x44,0x44,0x44,0x20, // c
  0x38,0x44,0x44,0x48,0x7F, // d
  0x38,0x54,0x54,0x54,0x18, // e
  0x08,0x7E,0x09,0x01,0x02, // f
  0x08,0x54,0x54,0x54,0x3C, // g
  0x7F,0x08,0x04,0x04,0x78, // h
  0x00,0x44,0x7D,0x40,0x00, // i
  0x20,0x40,0x44,0x3D,0x00, // j
  0x7F,0x10,0x28,0x44,0x00, // k
  0x00,0x41,0x7F,0x40,0x00, // l
  0x7C,0x04,0x18,0x04,0x78, // m
  0x7C,0x08,0x04,0x04,0x78, // n
  0x38,0x44,0x44,0x44,0x38, // o
  0x7C,0x14,0x14,0x14,0x08, // p
  0x08,0x14,0x14,0x14,0x7C, // q
  0x7C,0x08,0x04,0x04,0x08, // r
  0x48,0x54,0x54,0x54,0x20, // s
  0x04,0x3F,0x44,0x40,0x20, // t
  0x3C,0x40,0x40,0x20,0x7C, // u
  0x1C,0x20,0x40,0x20,0x1C, // v
  0x3C,0x40,0x30,0x40,0x3C, // w
  0x44,0x28,0x10,0x28,0x44, // x
  0x0C,0x50,0x50,0x50,0x3C, // y
  0x44,0x64,0x54,0x4C,0x44, // z
  0x00,0x08,0x36,0x41,0x00, // {
  0x00,0x00,0x7F,0x00,0x00, // |
  0x00,0x41,0x36,0x08,0x00, // }
  0x08,0x08,0x2A,0x1C,0x08, // ~
};

static constexpr int FONT_COLS   = 5;
static constexpr int FONT_ROWS   = 7;
static constexpr int FONT_GAP    = 1; // pixel gap between characters
static constexpr int FONT_CELL_W = FONT_COLS + FONT_GAP; // 6
static constexpr int FONT_CELL_H = FONT_ROWS + FONT_GAP; // 8

// ── Layout constants ─────────────────────────────────────────────────────────
// 58 mm paper at 203 DPI → printable width 384 dots.
static constexpr int PAPER_W     = 384;
static constexpr int BORDER      = 3;   // border thickness
static constexpr int MARGIN      = 5;   // inner margin
static constexpr int NAME_SCALE  = 2;   // product name font scale
static constexpr int LABEL_SCALE = 1;   // field label font scale
static constexpr int SEP_H       = 2;   // separator thickness
static constexpr int QR_MOD      = 4;   // dots per QR module
static constexpr int QR_VER      = 3;   // QR version 3 = 29×29 modules
static constexpr int QR_MODULES  = (QR_VER * 4 + 17); // 29
static constexpr int QR_SIZE     = QR_MODULES * QR_MOD; // 116 dots
static constexpr int NUM_ROWS    = 4;
static constexpr int ROW_H       = 14;  // height allocated per field row

// Derived X positions
static constexpr int X_INNER     = BORDER + MARGIN;
static constexpr int X_QR        = PAPER_W - BORDER - MARGIN - QR_SIZE;
static constexpr int X_TEXT_MAX  = X_QR - MARGIN;
static constexpr int TEXT_W      = X_TEXT_MAX - X_INNER;

// Derived Y positions
static constexpr int Y_NAME      = BORDER + MARGIN;
static constexpr int Y_SEP       = Y_NAME + FONT_CELL_H * NAME_SCALE + MARGIN;
static constexpr int Y_ROWS      = Y_SEP + SEP_H + 2;
static constexpr int Y_QR        = Y_ROWS;

// Canvas height: large enough for QR + bottom border
static constexpr int CANVAS_H    = Y_QR + QR_SIZE + MARGIN + BORDER;
static constexpr int BYTES_PER_ROW = PAPER_W / 8;

// ── BitmapLabel implementation ───────────────────────────────────────────────

BitmapLabel::BitmapLabel(EscPosPrinter &p) : printer(p) {}

BitmapLabel::~BitmapLabel() { freeCanvas(); }

bool BitmapLabel::allocCanvas(int w, int h) {
    freeCanvas();
    size_t sz = (size_t)(w / 8) * h;
    _canvas = (uint8_t *)heap_caps_calloc(sz, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_canvas) _canvas = (uint8_t *)calloc(sz, 1);
    if (!_canvas) return false;
    _canvasW = w;
    _canvasH = h;
    return true;
}

void BitmapLabel::freeCanvas() {
    if (_canvas) { free(_canvas); _canvas = nullptr; }
    _canvasW = _canvasH = 0;
}

void BitmapLabel::setPixel(int x, int y) {
    if (x < 0 || x >= _canvasW || y < 0 || y >= _canvasH) return;
    int byteIdx = y * (_canvasW / 8) + (x / 8);
    _canvas[byteIdx] |= (0x80 >> (x & 7)); // MSB = leftmost pixel
}

void BitmapLabel::drawHLine(int x, int y, int len) {
    for (int i = 0; i < len; i++) setPixel(x + i, y);
}

void BitmapLabel::drawVLine(int x, int y, int len) {
    for (int i = 0; i < len; i++) setPixel(x, y + i);
}

void BitmapLabel::drawRoundRect(int x, int y, int w, int h, int r) {
    // Straight segments
    drawHLine(x + r, y,         w - 2 * r); // top
    drawHLine(x + r, y + h - 1, w - 2 * r); // bottom
    drawVLine(x,         y + r, h - 2 * r); // left
    drawVLine(x + w - 1, y + r, h - 2 * r); // right
    // Corners (simple filled quarter-circle)
    for (int dy = 0; dy <= r; dy++) {
        for (int dx = 0; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                // top-left quadrant: only border pixels
                if ((dx - 1) * (dx - 1) + dy * dy > r * r ||
                    dx * dx + (dy - 1) * (dy - 1) > r * r) {
                    setPixel(x + r - dx, y + r - dy);
                    setPixel(x + w - 1 - r + dx, y + r - dy);
                    setPixel(x + r - dx, y + h - 1 - r + dy);
                    setPixel(x + w - 1 - r + dx, y + h - 1 - r + dy);
                }
            }
        }
    }
}

void BitmapLabel::drawChar(int x, int y, char c, int scale) {
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = '?';
    int idx = ((uint8_t)c - 0x20) * FONT_COLS;
    for (int col = 0; col < FONT_COLS; col++) {
        uint8_t colBits = pgm_read_byte(&FONT5x7[idx + col]);
        for (int row = 0; row < FONT_ROWS; row++) {
            if (colBits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        setPixel(x + col * scale + sx, y + row * scale + sy);
            }
        }
    }
}

void BitmapLabel::drawText(int x, int y, const char *text, int scale, int maxW) {
    int cx = x;
    int cellW = FONT_CELL_W * scale;
    while (*text) {
        if (maxW > 0 && (cx - x) + cellW > maxW) break;
        drawChar(cx, y, *text, scale);
        cx += cellW;
        text++;
    }
}

int BitmapLabel::textPixelWidth(const char *text, int scale) const {
    int len = strlen(text);
    return len * FONT_CELL_W * scale;
}

void BitmapLabel::drawTextCentered(int cx, int y, const char *text, int scale) {
    int tw = textPixelWidth(text, scale);
    drawText(cx - tw / 2, y, text, scale);
}

void BitmapLabel::drawUnderline(int x, int y, int textPixW, int scale) {
    int lineY = y + FONT_ROWS * scale + 1;
    drawHLine(x, lineY, textPixW);
}

void BitmapLabel::drawQR(int x, int y, const char *data, int moduleSize) {
    QRCode qrcode;
    // qrcode_getBufferSize(3) = 107 bytes; use fixed size for stack safety
    uint8_t qrBuf[128];
    if (!qrcode_initText(&qrcode, qrBuf, QR_VER, ECC_MEDIUM, data)) {
        qrcode_initText(&qrcode, qrBuf, QR_VER, ECC_LOW, data);
    }
    for (uint8_t qy = 0; qy < qrcode.size; qy++) {
        for (uint8_t qx = 0; qx < qrcode.size; qx++) {
            if (qrcode_getModule(&qrcode, qx, qy)) {
                for (int sy = 0; sy < moduleSize; sy++)
                    for (int sx = 0; sx < moduleSize; sx++)
                        setPixel(x + qx * moduleSize + sx, y + qy * moduleSize + sy);
            }
        }
    }
}

void BitmapLabel::sendCanvas() {
    // GS v 0: raster bit image
    // 1D 76 30 m xL xH yL yH [data...]
    uint16_t bytesPerLine = (uint16_t)(_canvasW / 8);
    uint16_t lines        = (uint16_t)_canvasH;

    printer.writeBytes((const uint8_t *)"\x1D\x76\x30\x00", 4);
    uint8_t hdr[4] = {
        (uint8_t)(bytesPerLine & 0xFF),
        (uint8_t)(bytesPerLine >> 8),
        (uint8_t)(lines & 0xFF),
        (uint8_t)(lines >> 8)
    };
    printer.writeBytes(hdr, 4);
    printer.writeBytes(_canvas, (size_t)bytesPerLine * lines);
}

// ── Public API ───────────────────────────────────────────────────────────────

bool BitmapLabel::printInventoryLabel(const InventoryItem &item) {
    if (!printer.isReady()) return false;

    if (!allocCanvas(PAPER_W, CANVAS_H)) return false;

    // Border
    drawRoundRect(0, 0, PAPER_W, CANVAS_H, BORDER + 2);

    // Product name (centered, large)
    String name = item.name.isEmpty() ? "Lebensmittel" : item.name;
    // Truncate so it fits within inner width at NAME_SCALE
    int maxNameChars = TEXT_W / (FONT_CELL_W * NAME_SCALE);
    if ((int)name.length() > maxNameChars) name = name.substring(0, maxNameChars);
    drawTextCentered(PAPER_W / 2, Y_NAME, name.c_str(), NAME_SCALE);

    // Separator
    drawHLine(X_INNER, Y_SEP, PAPER_W - 2 * X_INNER);
    if (SEP_H > 1) drawHLine(X_INNER, Y_SEP + 1, PAPER_W - 2 * X_INNER);

    // QR code
    String qrData = item.labelBarcode.isEmpty() ? item.name : item.labelBarcode;
    drawQR(X_QR, Y_QR, qrData.c_str(), QR_MOD);

    // Field rows (left column)
    struct Row { const char *label; String value; };
    String qty = String(item.quantity) + " St.";
    Row rows[NUM_ROWS] = {
        {"Einlagerung:", item.addedDate},
        {"MHD:",         item.expiryDate},
        {"Menge:",        qty},
        {"Haushalt:",     String(LABEL_HOUSEHOLD)},
    };

    for (int i = 0; i < NUM_ROWS; i++) {
        int ry = Y_ROWS + i * ROW_H + (ROW_H - FONT_CELL_H) / 2;

        // Bold label
        int lw = textPixelWidth(rows[i].label, LABEL_SCALE);
        drawText(X_INNER, ry, rows[i].label, LABEL_SCALE);
        // Underlined value
        int vx = X_INNER + lw + FONT_CELL_W; // one char gap
        int maxVW = X_QR - MARGIN - vx;
        if (maxVW > 0) {
            // Truncate value to fit
            String val = rows[i].value;
            while ((int)val.length() > 0 &&
                   textPixelWidth(val.c_str(), LABEL_SCALE) > maxVW) {
                val = val.substring(0, val.length() - 1);
            }
            int vw = textPixelWidth(val.c_str(), LABEL_SCALE);
            drawText(vx, ry, val.c_str(), LABEL_SCALE);
            drawUnderline(vx, ry, vw, LABEL_SCALE);
        }
    }

    printer.reset();
    sendCanvas();
    printer.feed(4);
    printer.flush();

    freeCanvas();
    return true;
}
