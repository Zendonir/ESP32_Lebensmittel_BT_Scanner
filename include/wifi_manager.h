#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"

enum WiFiMode {
    MODE_OFF,
    MODE_STATION
};

class WiFiManager {
public:
    WiFiManager();
    void init();
    void connectToWiFi(const char *ssid, const char *password);
    void saveCredentials(const char *ssid, const char *password);
    void loadCredentials(char *ssid, size_t ssidLen, char *password, size_t passLen);
    bool hasCredentials();
    bool autoConnect(uint32_t timeoutMs = 10000);
    String getSavedSSID();
    bool isConnected();
    WiFiMode getCurrentMode();
    String getIPAddress();
    int scanNetworks(bool includeHidden = true);
    void scan();
    String getSSID();
    void disconnect();

private:
    Preferences prefs;
    WiFiMode current_mode;
    String current_ssid;
};

extern WiFiManager wifi_manager;

#endif
