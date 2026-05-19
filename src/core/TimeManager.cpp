#include "TimeManager.h"
#include "Logger.h"
#include "wifi_manager.h"
#include <time.h>
#include <esp_sntp.h>

// ── BCD helpers ───────────────────────────────────────────────────────────────
static inline uint8_t bcdToDec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static inline uint8_t decToBcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

// ── NTP sync-complete flag (set by SNTP callback) ─────────────────────────────
static volatile bool s_ntpSynced = false;
static void ntpSyncCB(struct timeval *) { s_ntpSynced = true; }

// ── TimeManager ───────────────────────────────────────────────────────────────

TimeManager time_manager;

void TimeManager::begin(TwoWire &wire, const String &timezone) {
    _wire = &wire;

    // Apply the configured POSIX timezone string
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    Logger::info("Time", String("Timezone: ") + timezone);

    // Probe PCF85063A (I2C 0x51)
    wire.beginTransmission(PCF_ADDR);
    _rtcOk = (wire.endTransmission() == 0);
    Logger::info("RTC", _rtcOk ? "PCF85063 OK (0x51)" : "PCF85063 nicht gefunden");

    if (_rtcOk) {
        readRTC();
    }

    // Register NTP callback and configure servers (non-blocking)
    sntp_set_time_sync_notification_cb(ntpSyncCB);
    configTzTime(timezone.c_str(),
                 "pool.ntp.org", "de.pool.ntp.org", "time.google.com");
}

void TimeManager::readRTC() {
    if (!_wire || !_rtcOk) return;

    _wire->beginTransmission(PCF_ADDR);
    _wire->write(0x04);  // first time register: Seconds
    if (_wire->endTransmission(false) != 0) return;
    if (_wire->requestFrom((uint8_t)PCF_ADDR, (uint8_t)7) < 7) return;

    uint8_t secs    = bcdToDec(_wire->read() & 0x7F);  // bit7 = OS flag
    uint8_t mins    = bcdToDec(_wire->read() & 0x7F);
    uint8_t hours   = bcdToDec(_wire->read() & 0x3F);
    uint8_t days    = bcdToDec(_wire->read() & 0x3F);
    /* wday */       _wire->read();
    uint8_t months  = bcdToDec(_wire->read() & 0x1F);
    uint8_t years   = bcdToDec(_wire->read());         // 0–99 → 2000–2099

    struct tm t = {};
    t.tm_sec   = secs;
    t.tm_min   = mins;
    t.tm_hour  = hours;
    t.tm_mday  = days;
    t.tm_mon   = months - 1;     // tm_mon: 0–11
    t.tm_year  = years + 100;    // years since 1900; RTC is years since 2000
    t.tm_isdst = -1;

    time_t epoch = mktime(&t);
    if (epoch < 0) {
        Logger::warn("RTC", "Ungültige RTC-Zeit");
        return;
    }
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
    Logger::info("RTC", "Systemzeit aus RTC: " + today());
}

void TimeManager::writeRTC() {
    if (!_wire || !_rtcOk) return;

    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);

    _wire->beginTransmission(PCF_ADDR);
    _wire->write(0x04);
    _wire->write(decToBcd((uint8_t)t.tm_sec));
    _wire->write(decToBcd((uint8_t)t.tm_min));
    _wire->write(decToBcd((uint8_t)t.tm_hour));
    _wire->write(decToBcd((uint8_t)t.tm_mday));
    _wire->write((uint8_t)t.tm_wday);
    _wire->write(decToBcd((uint8_t)(t.tm_mon + 1)));
    _wire->write(decToBcd((uint8_t)(t.tm_year - 100)));
    uint8_t err = _wire->endTransmission();
    if (err == 0)
        Logger::info("RTC", "RTC aktualisiert: " + today());
    else
        Logger::warn("RTC", String("RTC Schreibfehler: ") + err);
}

void TimeManager::triggerNtp() {
    sntp_restart();
    _lastNtpTriggerMs = millis();
    Logger::info("NTP", "NTP-Sync gestartet");
}

void TimeManager::loop() {
    // If NTP just completed a sync, write the new time back to the RTC
    if (s_ntpSynced) {
        s_ntpSynced = false;
        Logger::info("NTP", "Uhrzeit synchronisiert: " + today());
        writeRTC();
    }

    // Trigger a fresh NTP sync every NTP_INTERVAL_MS (only when WiFi is up)
    if (wifi_manager.isConnected()) {
        uint32_t now = millis();
        if (_lastNtpTriggerMs == 0 || (now - _lastNtpTriggerMs) >= NTP_INTERVAL_MS) {
            triggerNtp();
        }
    }
}

bool TimeManager::isTimeSet() const {
    time_t now; time(&now);
    return now > 1700000000L;  // sanity: after 2023
}

String TimeManager::today() const {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return "1970-01-01";
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
    return String(buf);
}

String TimeManager::addDays(int days) const {
    time_t now; time(&now);
    now += static_cast<time_t>(days) * 86400;
    struct tm t; localtime_r(&now, &t);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return String(buf);
}
