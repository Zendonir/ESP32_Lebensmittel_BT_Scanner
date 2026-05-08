#include "wifi_manager.h"

WiFiManager wifi_manager;

WiFiManager::WiFiManager() : current_mode(MODE_OFF) {}

void WiFiManager::init() {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("ESP32-Scanner", "12345678");
    current_mode = MODE_AP_STA;

    Serial.println("\n[WiFi] AP Mode started:");
    Serial.print("SSID: ESP32-Scanner");
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void WiFiManager::startAP(const char *ssid, const char *password) {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP(ssid, password);
    current_mode = MODE_AP_STA;
    Serial.println("[WiFi] AP Mode: " + String(ssid));
}

void WiFiManager::connectToWiFi(const char *ssid, const char *password) {
    WiFi.begin(ssid, password);
    uint32_t timeout = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected!");
        Serial.println(WiFi.localIP());
    }
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
    return "ESP32-Scanner";
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
