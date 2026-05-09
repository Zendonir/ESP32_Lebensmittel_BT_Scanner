#include "BarcodeManager.h"

BarcodeManager barcode_manager;

static const char *typeName(BarcodeType type) {
    switch (type) {
        case BarcodeType::EAN8: return "EAN8";
        case BarcodeType::EAN13: return "EAN13";
        case BarcodeType::QR_OR_DATAMATRIX: return "QR/DataMatrix";
        case BarcodeType::LABEL: return "Label";
        case BarcodeType::UNKNOWN: return "Unknown";
    }
    return "Unknown";
}

void BarcodeManager::begin() {
    ble.begin();
}

void BarcodeManager::loop() {
    ble.loop();
}

bool BarcodeManager::readScan(ScanResult &result) {
    String raw;
    if (!ble.readCode(raw)) return false;

    result = parser.parse(raw);
    if (result.type == BarcodeType::UNKNOWN) return false;

    lastScan = result.code;
    lastType = typeName(result.type);
    return true;
}
