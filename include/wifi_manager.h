#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"

enum WiFiMode {
    MODE_OFF,
    MODE_STATION,
    MODE_AP,
    MODE_AP_STA
};

class WiFiManager {
public:
    WiFiManager();
    void init();
    void startAP(const char *ssid = AP_SSID, const char *password = AP_PASSWORD);
    void connectToWiFi(const char *ssid, const char *password);
    void saveCredentials(const char *ssid, const char *password);
    void loadCredentials(char *ssid, size_t ssidLen, char *password, size_t passLen);
    bool hasCredentials();
    bool autoConnect(uint32_t timeoutMs = 10000);
    String getSavedSSID();
    bool isConnected();
    bool isAPActive();
    void stopAP();
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
