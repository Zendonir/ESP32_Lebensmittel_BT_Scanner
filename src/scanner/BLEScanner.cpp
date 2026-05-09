#include "BLEScanner.h"

void BLEScanner::begin() {
    // NimBLE HID implementation hook: scan, pair, and append HID keystrokes to pendingCode.
}

void BLEScanner::loop() {}

bool BLEScanner::readCode(String &code) {
    if (pendingCode.isEmpty()) return false;
    code = pendingCode;
    pendingCode = "";
    return true;
}
