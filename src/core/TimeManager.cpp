#include "TimeManager.h"
#include <time.h>

void TimeManager::begin() {}

String TimeManager::today() const {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return "1970-01-01";
    char buffer[11];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
    return String(buffer);
}

String TimeManager::addDays(int days) const {
    time_t now;
    time(&now);
    now += static_cast<time_t>(days) * 86400;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buffer[11];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
    return String(buffer);
}
