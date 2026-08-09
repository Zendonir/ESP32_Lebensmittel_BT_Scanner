#include "PrinterManager.h"
#include "../core/Logger.h"
#include "config.h"

namespace {
const uint32_t BAUD_PROBE_RATES[] = { 9600, 19200, 38400, 57600, 115200 };
}

PrinterManager::PrinterManager() : renderer(printer) {}

void PrinterManager::begin() {
    if (!_queueMutex) _queueMutex = xSemaphoreCreateMutex();
    configure(UART_BAUD);
}

bool PrinterManager::queueLabel(const InventoryItem &item) {
    if (!_queueMutex) _queueMutex = xSemaphoreCreateMutex();
    if (!_queueMutex) return false;
    xSemaphoreTake(_queueMutex, portMAX_DELAY);
    bool ok = _queue.size() < MAX_QUEUE;
    if (ok) _queue.push_back(item);
    xSemaphoreGive(_queueMutex);
    if (!ok) Logger::warn("Printer", "Druckwarteschlange voll – Etikett verworfen");
    return ok;
}

size_t PrinterManager::queuedLabels() const {
    if (!_queueMutex) return 0;
    xSemaphoreTake(_queueMutex, portMAX_DELAY);
    size_t n = _queue.size();
    xSemaphoreGive(_queueMutex);
    return n;
}

void PrinterManager::processQueue() {
    if (!_queueMutex) return;
    InventoryItem item;
    {
        xSemaphoreTake(_queueMutex, portMAX_DELAY);
        bool empty = _queue.empty();
        if (!empty) {
            item = _queue.front();
            _queue.erase(_queue.begin());
        }
        xSemaphoreGive(_queueMutex);
        if (empty) return;
    }
    // Außerhalb des Locks drucken – der Web-Task darf währenddessen weiter
    // einreihen, ohne auf den Drucker zu warten.
    printLabel(item);
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

void PrinterManager::setPostFeed(uint16_t lines) {
    postFeedDots = lines;
}

void PrinterManager::printManual(const String &text, bool center) {
    if (!printer.isReady()) return;
    printer.reset();
    printer.setCodePage(16);
    printer.flush();
    delay(80);

    printer.setAlign(center ? 1 : 0);

    // Split on '\n' and print each line
    int start = 0;
    int len   = (int)text.length();
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            printer.println(text.substring(start, i));
            start = i + 1;
        }
    }

    printer.setAlign(0);
    printer.feedDots(postFeedDots);
    printer.flush();
}

void PrinterManager::printManualLabel(const String &text, bool center) {
    if (!printer.isReady()) return;

    // Double reset for clean state
    printer.reset();
    printer.flush();
    delay(40);
    printer.reset();
    printer.setCodePage(16);
    printer.flush();
    delay(80);

    // Count non-empty lines to drive auto-spacing
    int lineCount = 0;
    int start = 0;
    int len = (int)text.length();
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            if (i > start) lineCount++;  // skip blank lines for spacing calc
            start = i + 1;
        }
    }
    if (lineCount == 0) lineCount = 1;

    // Inventory label content height (double-height name + 4 rows × 24 + barcode ≈ 210 dots).
    // Distribute evenly across N lines so manual label uses the same paper length.
    // Content dots matches inventory label: double-height name + 4 rows×24 + barcode ≈ 210 dots.
    // ESC 3 n takes n as a 1-byte value (max 255 dots). Do not cap below max — few lines
    // with large spacing still consume the correct total paper (N × spacing = CONTENT_DOTS).
    const int CONTENT_DOTS = 210;
    int spacing = CONTENT_DOTS / lineCount;
    if (spacing < 24)  spacing = 24;   // minimum legible line advance
    if (spacing > 255) spacing = 255;  // ESC 3 n max
    bool doubleH = (spacing >= 48);

    printer.setLineSpacing((uint8_t)spacing);
    printer.setAlign(center ? 1 : 0);
    printer.setDoubleHeight(doubleH);

    // Print lines
    start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            printer.println(text.substring(start, i));
            start = i + 1;
        }
    }

    printer.setDoubleHeight(false);
    printer.setAlign(0);
    printer.feedDots(postFeedDots);
    printer.flush();
}

bool PrinterManager::printLabel(const InventoryItem &item) {
    renderer.setPostFeed(postFeedDots);
    bool ok = renderer.printInventoryLabel(item);
    if (ok) { _lastItem = item; _hasLastPrint = true; }
    return ok;
}

bool PrinterManager::printLast() {
    return _hasLastPrint ? printLabel(_lastItem) : false;
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
