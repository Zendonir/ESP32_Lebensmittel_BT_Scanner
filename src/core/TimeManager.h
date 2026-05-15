#pragma once

#include <Arduino.h>
#include <Wire.h>

class TimeManager {
public:
    void begin(TwoWire &wire);
    void loop();           // call from App::loop() — handles NTP sync + RTC write-back

    String today() const;
    String addDays(int days) const;
    bool   isTimeSet() const;

private:
    void readRTC();
    void writeRTC();
    void triggerNtp();

    TwoWire  *_wire            = nullptr;
    bool      _rtcOk           = false;
    uint32_t  _lastNtpTriggerMs = 0;

    static constexpr uint32_t NTP_INTERVAL_MS = 4UL * 3600000UL; // re-sync every 4 h
    static constexpr uint8_t  PCF_ADDR        = 0x51;
};

extern TimeManager time_manager;
