#pragma once
#include <Arduino.h>
#include <Preferences.h>

class DeviceConfig {
public:
    void   begin();
    String getHousehold()           const { return _household; }
    String getHouseholdAbbr()       const {
        if (!_householdAbbr.isEmpty()) return _householdAbbr;
        // Auto-generate: first 3 chars + "."
        return _household.length() > 3 ? _household.substring(0, 3) + "." : _household;
    }
    String getDeviceName()          const { return _deviceName; }
    String getActiveLocation()      const { return _activeLocation; }
    String getActiveLocationColor() const { return _activeLocationColor; }
    void   setHousehold(const String &h);
    void   setHouseholdAbbr(const String &abbr);
    void   setDeviceName(const String &n);
    void   setActiveLocation(const String &loc);
    void   setActiveLocationColor(const String &hexColor);
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
    String _ntfyUrl              = "https://ntfy.sh";
    String _ntfyTopic            = "";
    int    _ntfyDays             = 3;
};

extern DeviceConfig device_config;
