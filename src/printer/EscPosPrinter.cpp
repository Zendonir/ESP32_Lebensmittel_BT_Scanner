#include "EscPosPrinter.h"

void EscPosPrinter::begin(uint32_t baud) {
    if (ready) printerSerial.end();
    printerSerial.begin(baud, SERIAL_8N1, UART_RX, UART_TX);
    ready = true;
    delay(20);
    reset();
}

bool EscPosPrinter::isReady() const { return ready; }

void EscPosPrinter::reset() {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('@');
    printerSerial.flush();
    delay(50);
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
    // Left-align the label column (bold), right part is the value (underlined).
    // Pad so label+value fit exactly in paperChars; truncate value if needed.
    size_t labelLen = label.length();
    size_t available = (paperChars > labelLen + 1) ? (paperChars - labelLen) : 1;
    String val = value;
    if (val.length() > available) val = val.substring(0, available);
    // Pad value to fill remaining width (underline extends to edge).
    while (val.length() < available) val += ' ';

    setBold(true);
    printerSerial.print(label);
    setBold(false);
    setUnderline(true);
    size_t written = labelLen + printerSerial.print(val);
    setUnderline(false);
    written += printerSerial.write('\n');
    return written;
}

size_t EscPosPrinter::println(const String &text) {
    if (!ready) return 0;
    size_t written = printerSerial.print(text);
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

size_t EscPosPrinter::writeBytes(const uint8_t *data, size_t length) {
    if (!ready || data == nullptr || length == 0) return 0;
    return printerSerial.write(data, length);
}

size_t EscPosPrinter::writeText(const String &text) {
    if (!ready) return 0;
    return printerSerial.print(text);
}

void EscPosPrinter::flush() {
    if (!ready) return;
    printerSerial.flush();
}
