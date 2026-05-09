#include "BarcodeManager.h"

void BarcodeManager::begin() {
    uart.begin();
    ble.begin();
}

void BarcodeManager::loop() {
    ble.loop();
}

bool BarcodeManager::readScan(ScanResult &result) {
    String raw;
    if (uart.readCode(raw) || ble.readCode(raw)) {
        result = parser.parse(raw);
        return result.type != BarcodeType::UNKNOWN;
    }
    return false;
}
