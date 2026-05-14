#include "PrinterManager.h"
#include "../core/Logger.h"
#include "config.h"

namespace {
const uint32_t BAUD_PROBE_RATES[] = { 9600, 19200, 38400, 57600, 115200 };
}

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

void PrinterManager::feed(uint8_t lines) {
    if (!printer.isReady()) return;
    printer.feed(lines);
    printer.flush();
}

void PrinterManager::backFeed(uint8_t lines) {
    if (!printer.isReady()) return;
    printer.backFeed(lines);
    printer.flush();
}

bool PrinterManager::printLabel(const InventoryItem &item) {
    return renderer.printInventoryLabel(item);
}

size_t PrinterManager::printTestPage(bool includeQr) {
    if (!printer.isReady()) return 0;

    Logger::info("Printer", String("ESC/POS test print requested, qr=") + (includeQr ? 1 : 0) + " baud=" + currentBaud);
    printer.reset();
    size_t bytes = 2; // ESC @
    printer.setBold(true);
    bytes += 3;
    printer.setLarge(true);
    bytes += 3;
    bytes += printer.printlnCrLf("Testdruck");
    printer.setLarge(false);
    bytes += 3;
    bytes += printer.printlnCrLf("ESP32 Lebensmittel-Scanner");
    printer.setBold(false);
    bytes += 3;
    bytes += printer.printlnCrLf(String("UART TTL TX=") + UART_TX + " RX=" + UART_RX);
    bytes += printer.printlnCrLf(String("Baudrate: ") + currentBaud + " 8N1");
    bytes += printer.printlnCrLf("ESC/POS-Modus");
    bytes += printer.printlnCrLf("GND gemeinsam? RX am Drucker?");
    if (includeQr) {
        printer.feed(1);
        bytes += 3;
        bytes += printer.printlnCrLf("QR-Test:");
        printer.qrCode("ESP32-Lebensmittel-Scanner-Test");
        bytes += 40; // approximate ESC/POS QR command payload for diagnostics
    }
    printer.feed(4);
    bytes += 3;
    printer.flush();
    Logger::info("Printer", String("ESC/POS test print sent, bytes~") + static_cast<unsigned>(bytes));
    return bytes;
}

size_t PrinterManager::printPlainTest() {
    if (!printer.isReady()) return 0;

    Logger::info("Printer", String("Plain UART test print requested, baud=") + currentBaud);
    size_t bytes = 0;
    bytes += printer.printlnCrLf("");
    bytes += printer.printlnCrLf("=== UART TEST ===");
    bytes += printer.printlnCrLf("ESP32 Lebensmittel-Scanner");
    bytes += printer.printlnCrLf(String("TX=") + UART_TX + " RX=" + UART_RX + " baud=" + currentBaud + " 8N1");
    bytes += printer.printlnCrLf("Wenn lesbar: UART ist korrekt.");
    bytes += printer.printlnCrLf("");
    bytes += printer.printlnCrLf("");
    printer.flush();
    Logger::info("Printer", String("Plain UART test sent, bytes=") + static_cast<unsigned>(bytes));
    return bytes;
}

size_t PrinterManager::printBaudProbe() {
    if (!printer.isReady()) return 0;

    uint32_t restoreBaud = currentBaud ? currentBaud : UART_BAUD;
    size_t bytes = 0;
    Logger::info("Printer", "Baud probe requested");
    for (uint32_t rate : BAUD_PROBE_RATES) {
        configure(rate);
        delay(120);
        bytes += printer.printlnCrLf("");
        bytes += printer.printlnCrLf(String("BAUD TEST ") + rate + " 8N1");
        bytes += printer.printlnCrLf("Wenn lesbar, diese Baudrate speichern.");
        bytes += printer.printlnCrLf("");
        printer.flush();
        delay(250);
    }
    configure(restoreBaud);
    Logger::info("Printer", String("Baud probe sent, bytes=") + static_cast<unsigned>(bytes) + " restored=" + restoreBaud);
    return bytes;
}
