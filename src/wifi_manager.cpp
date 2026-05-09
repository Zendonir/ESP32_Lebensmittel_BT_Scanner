#include "wifi_manager.h"

WiFiManager wifi_manager;

WiFiManager::WiFiManager() : current_mode(MODE_OFF) {}

void WiFiManager::init() {
    WiFi.mode(WIFI_MODE_APSTA);
    current_ssid = AP_SSID;
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    current_mode = MODE_AP_STA;

    Serial.println("\n[WiFi] AP Mode started:");
    Serial.print("SSID: ");
    Serial.println(current_ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
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
    WiFi.begin(ssid, password ? password : "");
    current_mode = MODE_STATION;
    Serial.print("[WiFi] Connecting asynchronously to: ");
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

void WiFiManager::saveCredentials(const char *ssid, const char *password) {}
void WiFiManager::loadCredentials(char *ssid, char *password) {}
