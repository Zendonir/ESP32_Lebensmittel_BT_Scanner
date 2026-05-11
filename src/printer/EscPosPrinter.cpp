#include "EscPosPrinter.h"

void EscPosPrinter::begin(uint32_t baud) {
    printerSerial.begin(baud, SERIAL_8N1, UART_RX, UART_TX);
    ready = true;
    reset();
}

bool EscPosPrinter::isReady() const { return ready; }

void EscPosPrinter::reset() {
    if (!ready) return;
    printerSerial.write(0x1B);
    printerSerial.write('@');
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

void EscPosPrinter::println(const String &text) {
    if (!ready) return;
    printerSerial.print(text);
    printerSerial.write('\n');
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

void EscPosPrinter::flush() {
    if (!ready) return;
    printerSerial.flush();
}
