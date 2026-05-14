#include "EscPosPrinter.h"

// Convert UTF-8 string to Windows-1252 single-byte encoding.
// All German/Western-European characters (Ä Ö Ü ä ö ü ß etc.) are in the
// Unicode range U+00A0–U+00FF which maps 1:1 to WPC1252 bytes 0xA0–0xFF.
static String utf8ToWin1252(const String &s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); ) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) {
            out += (char)c; i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.length()) {
            uint16_t cp = ((c & 0x1F) << 6) | ((uint8_t)s[i+1] & 0x3F);
            out += (char)((cp >= 0xA0 && cp <= 0xFF) ? (uint8_t)cp : '?');
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.length()) {
            out += '?'; i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.length()) {
            out += '?'; i += 4;
        } else {
            i++;
        }
    }
    return out;
}

void EscPosPrinter::begin(uint32_t baud) {
    if (ready) printerSerial.end();
    printerSerial.begin(baud, SERIAL_8N1, UART_RX, UART_TX);
    ready = true;
    delay(100);
    // 0x18 (CAN) discards residual bytes; ESC @ resets formatting.
    printerSerial.write((uint8_t)0x18);
    printerSerial.flush();
    delay(50);
    reset();
    // Select Windows-1252 code page so ÄÖÜäöüß print correctly.
    setCodePage(16);
}

bool EscPosPrinter::isReady() const { return ready; }

void EscPosPrinter::reset() {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('@');
    printerSerial.flush();
    delay(50);
}

void EscPosPrinter::setLineSpacing(uint8_t n) {
    if (!ready) return;
    if (n == 0) {
        // ESC 2 restores default line spacing
        printerSerial.write(0x1B);
        printerSerial.write('2');
    } else {
        printerSerial.write(0x1B);
        printerSerial.write('3');
        printerSerial.write(n);
    }
}

void EscPosPrinter::setBold(bool enabled) {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('E');
    printerSerial.write(enabled ? 1 : 0);
}

void EscPosPrinter::setLarge(bool enabled) {
    if (!ready) return;
    printerSerial.write(0x1D);
    printerSerial.write('!');
    printerSerial.write(enabled ? 0x11 : 0x00);
}

void EscPosPrinter::setUnderline(bool enabled) {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('-');
    printerSerial.write(enabled ? 1 : 0);
}

void EscPosPrinter::setAlign(uint8_t align) {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('a');
    printerSerial.write(align & 0x03);
}

size_t EscPosPrinter::printLabelRow(const String &label, const String &value,
                                    uint8_t paperChars) {
    if (!ready) return 0;
    // Truncate value if label+value would exceed paperChars; no space padding –
    // the underline covers only the actual value characters (clean look).
    size_t labelLen = label.length();
    size_t maxVal = (paperChars > labelLen) ? (paperChars - labelLen) : 1;
    String val = value;
    if (val.length() > maxVal) val = val.substring(0, maxVal);

    setBold(true);
    printerSerial.print(label);
    setBold(false);
    setUnderline(true);
    printerSerial.print(val);
    setUnderline(false);
    size_t written = labelLen + val.length();
    written += printerSerial.write('\n');
    return written;
}

size_t EscPosPrinter::println(const String &text) {
    if (!ready) return 0;
    String converted = utf8ToWin1252(text);
    size_t written = printerSerial.print(converted);
    written += printerSerial.write('\n');
    return written;
}

size_t EscPosPrinter::printlnCrLf(const String &text) {
    if (!ready) return 0;
    size_t written = printerSerial.print(text);
    written += printerSerial.write('\r');
    written += printerSerial.write('\n');
    return written;
}

void EscPosPrinter::feed(uint8_t lines) {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('d');
    printerSerial.write(lines);
}

void EscPosPrinter::feedDots(uint16_t dots) {
    if (!ready || dots == 0) return;
    while (dots > 0) {
        uint8_t chunk = dots > 255 ? 255 : (uint8_t)dots;
        printerSerial.write(0x1B);
        printerSerial.write('J');  // ESC J — print and feed n dots
        printerSerial.write(chunk);
        dots -= chunk;
    }
}

void EscPosPrinter::backFeed(uint8_t lines) {
    rawBackfeed(0x4B, (uint16_t)lines * 24, 0, 50, true);
}

void EscPosPrinter::rawBackfeed(uint8_t cmd, uint16_t dots, uint8_t chunkSize,
                                 uint16_t delayMs, bool doFlush) {
    if (!ready || dots == 0) return;
    if (chunkSize == 0) {
        // Single command – cap at 255 (ESC/POS parameter is 1 byte)
        uint8_t n = (dots > 255) ? 255 : (uint8_t)dots;
        printerSerial.write(0x1B);
        printerSerial.write(cmd);
        printerSerial.write(n);
        if (doFlush) printerSerial.flush();
        if (delayMs) delay(delayMs);
    } else {
        while (dots > 0) {
            uint8_t n = (dots > chunkSize) ? chunkSize : (uint8_t)dots;
            printerSerial.write(0x1B);
            printerSerial.write(cmd);
            printerSerial.write(n);
            if (doFlush) printerSerial.flush();
            if (delayMs) delay(delayMs);
            dots -= n;
        }
    }
}

void EscPosPrinter::qrCode(const String &data) {
    if (!ready) return;
    uint16_t storeLen = data.length() + 3;
    printerSerial.write(0x1D); printerSerial.write('('); printerSerial.write('k');
    printerSerial.write(3); printerSerial.write(0); printerSerial.write(49); printerSerial.write(67); printerSerial.write(6);
    printerSerial.write(0x1D); printerSerial.write('('); printerSerial.write('k');
    printerSerial.write(3); printerSerial.write(0); printerSerial.write(49); printerSerial.write(69); printerSerial.write(48);
    printerSerial.write(0x1D); printerSerial.write('('); printerSerial.write('k');
    printerSerial.write(storeLen & 0xFF); printerSerial.write(storeLen >> 8);
    printerSerial.write(49); printerSerial.write(80); printerSerial.write(48);
    printerSerial.print(data);
    printerSerial.write(0x1D); printerSerial.write('('); printerSerial.write('k');
    printerSerial.write(3); printerSerial.write(0); printerSerial.write(49); printerSerial.write(81); printerSerial.write(48);
}

void EscPosPrinter::barcodeHeight(uint8_t dots) {
    if (!ready) return;
    printerSerial.write(0x1D); printerSerial.write('h'); printerSerial.write(dots);
}

void EscPosPrinter::barcodeWidth(uint8_t mult) {
    if (!ready) return;
    printerSerial.write(0x1D); printerSerial.write('w'); printerSerial.write(mult);
}

void EscPosPrinter::barcodeHRI(uint8_t pos) {
    if (!ready) return;
    printerSerial.write(0x1D); printerSerial.write('H'); printerSerial.write(pos);
}

void EscPosPrinter::barcode128(const String &data) {
    if (!ready || data.isEmpty()) return;
    // GS k m n d1..dk  (function-B format), Code 128B subset ("{B" prefix)
    uint8_t n = (uint8_t)(2 + data.length());
    printerSerial.write(0x1D);
    printerSerial.write('k');
    printerSerial.write((uint8_t)73); // m = 73 = Code 128
    printerSerial.write(n);
    printerSerial.write('{');
    printerSerial.write('B');
    printerSerial.print(data);
}

size_t EscPosPrinter::writeBytes(const uint8_t *data, size_t length) {
    if (!ready || data == nullptr || length == 0) return 0;
    return printerSerial.write(data, length);
}

size_t EscPosPrinter::writeText(const String &text) {
    if (!ready) return 0;
    String converted = utf8ToWin1252(text);
    return printerSerial.print(converted);
}

void EscPosPrinter::flush() {
    if (!ready) return;
    printerSerial.flush();
}

void EscPosPrinter::setCodePage(uint8_t n) {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('t');
    printerSerial.write(n);
}

void EscPosPrinter::setDoubleHeight(bool enabled) {
    if (!ready) return;
    // GS ! n — bits 0-2: height mult (0=1x,1=2x), bits 4-6: width mult
    // 0x01 = double height only, normal width → more chars fit per line
    printerSerial.write(0x1D);
    printerSerial.write('!');
    printerSerial.write(enabled ? (uint8_t)0x01 : (uint8_t)0x00);
}
