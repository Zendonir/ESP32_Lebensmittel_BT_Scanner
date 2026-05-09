#pragma once

#include "BLEScanner.h"
#include "ScanParser.h"

class BarcodeManager {
public:
    void begin();
    void loop();
    bool readScan(ScanResult &result);

    BLEScanner &scanner() { return ble; }
    String getLastScan() const { return lastScan; }
    String getLastType() const { return lastType; }

private:
    BLEScanner &ble = ble_scanner;
    ScanParser parser;
    String lastScan;
    String lastType;
};

extern BarcodeManager barcode_manager;
