#pragma once

#include <Arduino.h>

enum class BarcodeType {
    UNKNOWN,
    EAN8,
    EAN13,
    QR_OR_DATAMATRIX,
    LABEL
};

struct ScanResult {
    String code;
    BarcodeType type = BarcodeType::UNKNOWN;
};

class ScanParser {
public:
    ScanResult parse(const String &raw) const;
};
