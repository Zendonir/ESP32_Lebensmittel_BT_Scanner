#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

class DeviceConfig {
public:
    void   begin();
    String getHousehold()           const { return _household; }
    String getHouseholdAbbr()       const {
        if (!_householdAbbr.isEmpty()) return _householdAbbr;
        return _household.length() > 3 ? _household.substring(0, 3) + "." : _household;
    }
    // Falls kein Name konfiguriert, Chip-ID als eindeutiger Fallback
    String getDeviceName() const {
        if (!_deviceName.isEmpty()) return _deviceName;
        uint64_t chipId = ESP.getEfuseMac();
        char buf[16];
        snprintf(buf, sizeof(buf), "ESP32-%06llX", (unsigned long long)(chipId & 0xFFFFFF));
        return String(buf);
    }
    String getActiveLocation()      const { return _activeLocation; }
    String getActiveLocationColor() const { return _activeLocationColor; }
    String getTimezone()            const { return _timezone; }
    void   setHousehold(const String &h);
    void   setHouseholdAbbr(const String &abbr);
    void   setDeviceName(const String &n);
    void   setActiveLocation(const String &loc);
    void   setActiveLocationColor(const String &hexColor);
    void   setTimezone(const String &tz);
    String getNtfyUrl()   const { return _ntfyUrl; }
    String getNtfyTopic() const { return _ntfyTopic; }
    int    getNtfyDays()  const { return _ntfyDays; }
    void   setNtfyUrl(const String &u);
    void   setNtfyTopic(const String &t);
    void   setNtfyDays(int d);

private:
    String _household            = "Standard";
    String _householdAbbr        = "";
    String _deviceName           = "";
    String _activeLocation       = "";
    String _activeLocationColor  = "";  // "#RRGGBB", empty = accent blue default
    String _timezone             = "CET-1CEST,M3.5.0,M10.5.0/3";  // POSIX TZ-String
    String _ntfyUrl              = "https://ntfy.sh";
    String _ntfyTopic            = "";
    int    _ntfyDays             = 3;
};

extern DeviceConfig device_config;
