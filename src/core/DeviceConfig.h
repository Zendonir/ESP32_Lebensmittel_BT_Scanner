#pragma once
#include <Arduino.h>
#include <Preferences.h>

class DeviceConfig {
public:
    void   begin();
    String getHousehold()  const { return _household; }
    String getDeviceName() const { return _deviceName; }
    void   setHousehold(const String &h);
    void   setDeviceName(const String &n);

private:
    String _household  = "Standard";
    String _deviceName = "";
};

extern DeviceConfig device_config;
