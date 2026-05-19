#include "ScanParser.h"

ScanResult ScanParser::parse(const String &raw) const {
    ScanResult result;
    result.code = raw;
    result.code.trim();
    if (result.code.startsWith("LebNumber")) result.type = BarcodeType::LABEL;
    else if (result.code.length() == 13) result.type = BarcodeType::EAN13;
    else if (result.code.length() == 8) result.type = BarcodeType::EAN8;
    else if (!result.code.isEmpty()) result.type = BarcodeType::QR_OR_DATAMATRIX;
    return result;
}
