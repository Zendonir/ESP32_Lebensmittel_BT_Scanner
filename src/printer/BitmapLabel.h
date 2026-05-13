#pragma once
#include <Arduino.h>
#include "EscPosPrinter.h"
#include "../models/InventoryItem.h"

// Renders a label as a 1-bit raster bitmap and sends it via ESC/POS GS v 0.
// This allows true side-by-side placement of text and the QR code.
class BitmapLabel {
public:
    explicit BitmapLabel(EscPosPrinter &printer);
    ~BitmapLabel();

    bool printInventoryLabel(const InventoryItem &item);

private:
    EscPosPrinter &printer;
    uint8_t *_canvas = nullptr;
    int _canvasW = 0;   // dots, always a multiple of 8
    int _canvasH = 0;

    bool allocCanvas(int w, int h);
    void freeCanvas();
    void setPixel(int x, int y);

    void drawHLine(int x, int y, int len);
    void drawVLine(int x, int y, int len);
    void drawRoundRect(int x, int y, int w, int h, int r);

    // 5×7 font, column format; scale≥1 nearest-neighbour upscale
    void drawChar(int x, int y, char c, int scale);
    void drawText(int x, int y, const char *text, int scale, int maxW = 0);
    void drawTextCentered(int cx, int y, const char *text, int scale);
    int  textPixelWidth(const char *text, int scale) const;

    // Draw underline under text
    void drawUnderline(int x, int y, int textPixW, int scale);

    void drawQR(int x, int y, const char *data, int moduleSize);

    void sendCanvas();
};
