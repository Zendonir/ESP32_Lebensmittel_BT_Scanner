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

void LabelCounter::newRoll(uint32_t size) {
    uint32_t cur = nvs.getUInt("cnt", 0);
    nvs.putUInt("rollSize",  size);
    nvs.putUInt("rollStart", cur);
}

int LabelCounter::getRemaining() {
    uint32_t size = nvs.getUInt("rollSize", 0);
    if (size == 0) return -1;
    uint32_t start = nvs.getUInt("rollStart", 0);
    uint32_t cur   = nvs.getUInt("cnt", 0);
    uint32_t used  = (cur >= start) ? (cur - start) : 0;
    return (used < size) ? (int)(size - used) : 0;
}

uint32_t LabelCounter::getRollSize() {
    return nvs.getUInt("rollSize", 0);
}
