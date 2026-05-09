#include "LabelCounter.h"

bool LabelCounter::begin() {
    return nvs.begin("lager");
}

String LabelCounter::nextLabel() {
    uint32_t counter = nvs.getUInt("cnt", 0) + 1;
    nvs.putUInt("cnt", counter);
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "LebNumber%05lu", static_cast<unsigned long>(counter));
    return String(buffer);
}
