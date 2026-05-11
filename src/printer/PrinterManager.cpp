#include "PrinterManager.h"
#include "../core/Logger.h"
#include "config.h"

PrinterManager::PrinterManager() : renderer(printer) {}

void PrinterManager::begin() {
    configure(UART_BAUD);
}

void PrinterManager::configure(uint32_t baud) {
    if (baud == 0) baud = UART_BAUD;
    currentBaud = baud;
    printer.begin(currentBaud);
    Logger::info("Printer", String("TTL UART ready on TX=") + UART_TX + " RX=" + UART_RX + " baud=" + currentBaud);
}

bool PrinterManager::isReady() const {
    return printer.isReady();
}

uint32_t PrinterManager::baud() const {
    return currentBaud;
}

bool PrinterManager::printLabel(const InventoryItem &item) {
    return renderer.printInventoryLabel(item);
}

bool PrinterManager::printTestPage(bool includeQr) {
    if (!printer.isReady()) return false;

    printer.reset();
    printer.setBold(true);
    printer.setLarge(true);
    printer.println("Testdruck");
    printer.setLarge(false);
    printer.println("ESP32 Lebensmittel-Scanner");
    printer.setBold(false);
    printer.println(String("UART TTL TX=") + UART_TX + " RX=" + UART_RX);
    printer.println(String("Baudrate: ") + currentBaud);
    printer.println("Wenn diese Zeilen erscheinen,");
    printer.println("ist der Drucker verbunden.");
    if (includeQr) {
        printer.feed(1);
        printer.println("QR-Test:");
        printer.qrCode("ESP32-Lebensmittel-Scanner-Test");
    }
    printer.feed(4);
    printer.flush();
    return true;
}
