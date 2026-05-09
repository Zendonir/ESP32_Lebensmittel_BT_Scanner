#pragma once

#include "UARTScanner.h"
#include "BLEScanner.h"
#include "ScanParser.h"

class BarcodeManager {
public:
    void begin();
    void loop();
    bool readScan(ScanResult &result);

private:
    UARTScanner uart;
    BLEScanner ble;
    ScanParser parser;
};
