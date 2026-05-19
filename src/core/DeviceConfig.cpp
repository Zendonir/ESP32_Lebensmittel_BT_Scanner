#include "DeviceConfig.h"

DeviceConfig device_config;

void DeviceConfig::begin() {
    Preferences p;
    if (!p.begin("device", false)) return;  // false = read-write, creates namespace on first boot
    _household            = p.getString("household", "Standard");
    _householdAbbr        = p.getString("hhAbbr",    "");
    _deviceName           = p.getString("devName",   "");
    _activeLocation       = p.getString("location",  "");
    _activeLocationColor  = p.getString("locColor",  "");
    _timezone             = p.getString("timezone",  "CET-1CEST,M3.5.0,M10.5.0/3");
    _ntfyUrl              = p.getString("ntfyUrl",   "https://ntfy.sh");
    _ntfyTopic            = p.getString("ntfyTopic", "");
    _ntfyDays             = p.getInt("ntfyDays",     3);
    p.end();
}

void DeviceConfig::setHousehold(const String &h) {
    _household = h.isEmpty() ? "Standard" : h;
    Preferences p;
    p.begin("device", false);
    p.putString("household", _household);
    p.end();
}

void DeviceConfig::setHouseholdAbbr(const String &abbr) {
    _householdAbbr = abbr;
    Preferences p;
    p.begin("device", false);
    p.putString("hhAbbr", _householdAbbr);
    p.end();
}

void DeviceConfig::setDeviceName(const String &n) {
    _deviceName = n;
    Preferences p;
    p.begin("device", false);
    p.putString("devName", _deviceName);
    p.end();
}

void DeviceConfig::setActiveLocation(const String &loc) {
    _activeLocation = loc;
    Preferences p;
    p.begin("device", false);
    p.putString("location", _activeLocation);
    p.end();
}

void DeviceConfig::setActiveLocationColor(const String &hexColor) {
    _activeLocationColor = hexColor;
    Preferences p;
    p.begin("device", false);
    p.putString("locColor", _activeLocationColor);
    p.end();
}

void DeviceConfig::setNtfyUrl(const String &u) {
    _ntfyUrl = u.isEmpty() ? "https://ntfy.sh" : u;
    Preferences p; p.begin("device", false);
    p.putString("ntfyUrl", _ntfyUrl); p.end();
}

void DeviceConfig::setNtfyTopic(const String &t) {
    _ntfyTopic = t;
    Preferences p; p.begin("device", false);
    p.putString("ntfyTopic", _ntfyTopic); p.end();
}

void DeviceConfig::setNtfyDays(int d) {
    _ntfyDays = max(1, min(d, 30));
    Preferences p; p.begin("device", false);
    p.putInt("ntfyDays", _ntfyDays); p.end();
}

void DeviceConfig::setTimezone(const String &tz) {
    _timezone = tz.isEmpty() ? "CET-1CEST,M3.5.0,M10.5.0/3" : tz;
    Preferences p; p.begin("device", false);
    p.putString("timezone", _timezone); p.end();
}
