#pragma once
#include <Arduino.h>
#include <Preferences.h>

class DeviceConfig {
public:
    void   begin();
    String getHousehold()      const { return _household; }
    String getDeviceName()     const { return _deviceName; }
    String getActiveLocation() const { return _activeLocation; }
    void   setHousehold(const String &h);
    void   setDeviceName(const String &n);
    void   setActiveLocation(const String &loc);

private:
    String _household      = "Standard";
    String _deviceName     = "";
    String _activeLocation = "";
};

extern DeviceConfig device_config;
