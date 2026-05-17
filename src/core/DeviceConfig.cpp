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
