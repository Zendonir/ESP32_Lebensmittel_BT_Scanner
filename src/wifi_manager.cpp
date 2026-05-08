#include "wifi_manager.h"

WiFiManager wifi_manager;

WiFiManager::WiFiManager() : current_mode(MODE_OFF) {}

void WiFiManager::init() {
    prefs.begin("wifi", false);
    WiFi.mode(WIFI_MODE_STA);
    startAP();
}

void WiFiManager::startAP(const char *ssid, const char *password) {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP(ssid, password);
    current_mode = MODE_AP_STA;

    Serial.println("\n[WiFi] AP Mode started:");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    startWebServer();
}

void WiFiManager::connectToWiFi(const char *ssid, const char *password) {
    Serial.print("[WiFi] Connecting to: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    uint32_t timeout = millis() + 20000; // 20 second timeout
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        current_ssid = ssid;
        saveCredentials(ssid, password);
    } else {
        Serial.println("\n[WiFi] Connection failed!");
    }
}

void WiFiManager::saveCredentials(const char *ssid, const char *password) {
    prefs.putString(WIFI_SSID_STORAGE, ssid);
    prefs.putString(WIFI_PASS_STORAGE, password);
}

void WiFiManager::loadCredentials(char *ssid, char *password) {
    String stored_ssid = prefs.getString(WIFI_SSID_STORAGE, "");
    String stored_pass = prefs.getString(WIFI_PASS_STORAGE, "");

    strcpy(ssid, stored_ssid.c_str());
    strcpy(password, stored_pass.c_str());
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

WiFiMode WiFiManager::getCurrentMode() {
    return current_mode;
}

String WiFiManager::getIPAddress() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return WiFi.softAPIP().toString();
}

void WiFiManager::scan() {
    Serial.println("[WiFi] Starting scan...");
    int n = WiFi.scanNetworks();
    Serial.print("[WiFi] Found ");
    Serial.print(n);
    Serial.println(" networks");

    for (int i = 0; i < n; i++) {
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(WiFi.SSID(i));
        Serial.print(" (");
        Serial.print(WiFi.RSSI(i));
        Serial.println(" dBm)");
    }
}

String WiFiManager::getSSID() {
    if (!current_ssid.isEmpty()) {
        return current_ssid;
    }
    return WiFi.SSID();
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true); // true = turn off WiFi radio
    current_mode = MODE_OFF;
    Serial.println("[WiFi] Disconnected");
}

void WiFiManager::startWebServer() {
    Serial.println("[WiFi] Web server optional - implement if needed");
}
