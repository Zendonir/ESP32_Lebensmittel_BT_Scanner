#include "wifi_manager.h"
#include <Preferences.h>

WiFiManager wifi_manager;

WiFiManager::WiFiManager() : current_mode(MODE_OFF) {}

void WiFiManager::init() {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    current_mode = MODE_AP_STA;

    Serial.println("\n[WiFi] AP started: " AP_SSID);
    Serial.print("[WiFi] AP IP: ");
    Serial.println(WiFi.softAPIP());

    if (autoConnect(10000)) {
        Serial.print("[WiFi] Station connected, IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[WiFi] No saved credentials or auto-connect failed – AP-only mode");
    }
    current_ssid = wifi_manager.isConnected() ? WiFi.SSID() : String(AP_SSID);
}

void WiFiManager::saveCredentials(const char *ssid, const char *password) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid   ? ssid     : "");
    prefs.putString("pass", password ? password : "");
    prefs.end();
}

void WiFiManager::loadCredentials(char *ssid, size_t ssidLen,
                                  char *password, size_t passLen) {
    prefs.begin("wifi", true);
    String s = prefs.getString("ssid", "");
    String p = prefs.getString("pass", "");
    prefs.end();
    strncpy(ssid,     s.c_str(), ssidLen - 1); ssid[ssidLen - 1]         = '\0';
    strncpy(password, p.c_str(), passLen - 1); password[passLen - 1]     = '\0';
}

bool WiFiManager::hasCredentials() {
    prefs.begin("wifi", true);
    String s = prefs.getString("ssid", "");
    prefs.end();
    return s.length() > 0;
}

String WiFiManager::getSavedSSID() {
    prefs.begin("wifi", true);
    String s = prefs.getString("ssid", "");
    prefs.end();
    return s;
}

bool WiFiManager::autoConnect(uint32_t timeoutMs) {
    char ssid[64] = {}, pass[64] = {};
    loadCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] == '\0') return false;

    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        current_ssid = ssid;
        return true;
    }
    WiFi.disconnect(false);
    return false;
}

void WiFiManager::startAP(const char *ssid, const char *password) {
    WiFi.mode(WIFI_MODE_APSTA);
    current_ssid = ssid ? ssid : AP_SSID;
    WiFi.softAP(current_ssid.c_str(), password ? password : AP_PASSWORD);
    current_mode = MODE_AP_STA;
    Serial.print("[WiFi] AP Mode: ");
    Serial.println(current_ssid);
}

void WiFiManager::connectToWiFi(const char *ssid, const char *password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        Serial.println("[WiFi] Missing SSID, staying in AP mode");
        return;
    }
    current_ssid = ssid;
    WiFi.begin(ssid, password && password[0] ? password : nullptr);
    current_mode = MODE_AP_STA;
    Serial.print("[WiFi] Connecting to: ");
    Serial.println(current_ssid);
}

void WiFiManager::scan() {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        Serial.println(WiFi.SSID(i));
    }
}

String WiFiManager::getIPAddress() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return WiFi.softAPIP().toString();
}

String WiFiManager::getSSID() {
    return current_ssid.isEmpty() ? String(AP_SSID) : current_ssid;
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    current_mode = MODE_OFF;
}

WiFiMode WiFiManager::getCurrentMode() {
    return current_mode;
}
