#include "DeviceConfig.h"

DeviceConfig device_config;

void DeviceConfig::begin() {
    Preferences p;
    if (!p.begin("device", true)) return;
    _household  = p.getString("household", "Standard");
    _deviceName = p.getString("devName",   "");
    p.end();
}

void DeviceConfig::setHousehold(const String &h) {
    _household = h.isEmpty() ? "Standard" : h;
    Preferences p;
    p.begin("device", false);
    p.putString("household", _household);
    p.end();
}

void DeviceConfig::setDeviceName(const String &n) {
    _deviceName = n;
    Preferences p;
    p.begin("device", false);
    p.putString("devName", _deviceName);
    p.end();
}
