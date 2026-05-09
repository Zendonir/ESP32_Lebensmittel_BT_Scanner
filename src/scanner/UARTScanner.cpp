#include "UARTScanner.h"

void UARTScanner::begin(uint32_t baud) {
    scannerSerial.begin(baud, SERIAL_8N1, UART_RX, UART_TX);
}

bool UARTScanner::readCode(String &code) {
    if (!scannerSerial.available()) return false;
    code = scannerSerial.readStringUntil('\n');
    code.trim();
    return !code.isEmpty();
}
