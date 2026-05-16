#include "wifi_manager.h"
#include <Preferences.h>

WiFiManager wifi_manager;

WiFiManager::WiFiManager() : current_mode(MODE_OFF) {}

void WiFiManager::init() {
    WiFi.persistent(false);
    WiFi.setSleep(false);

    // Start AP+STA so the setup page is reachable while we try to connect.
    WiFi.mode(WIFI_MODE_APSTA);
    delay(100);

    if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.println("\n[WiFi] AP started: " AP_SSID);
        Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] ERROR: AP start failed");
    }
    Serial.flush();
    current_mode = MODE_AP_STA;

    if (autoConnect(20000)) {
        Serial.printf("[WiFi] Station connected, IP: %s\n", WiFi.localIP().toString().c_str());
        // WiFi is up – shut the AP down so the hotspot is invisible and
        // captive-portal redirects don't intercept normal browser traffic.
        stopAP();
    } else {
        Serial.println("[WiFi] No saved credentials or auto-connect failed – AP-only mode");
        // Switch to pure AP mode (saves a tiny bit of power, avoids ghost STA).
        WiFi.mode(WIFI_MODE_AP);
        current_mode = MODE_AP;
    }
    current_ssid = isConnected() ? WiFi.SSID() : String(AP_SSID);
    Serial.flush();
}

void WiFiManager::saveCredentials(const char *ssid, const char *password) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid   ? ssid     : "");
    prefs.putString("pass", password ? password : "");
    prefs.end();
}

void WiFiManager::loadCredentials(char *ssid, size_t ssidLen,
                                  char *password, size_t passLen) {
    prefs.begin("wifi", false);
    String s = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
    String p = prefs.isKey("pass") ? prefs.getString("pass", "") : "";
    prefs.end();
    strncpy(ssid,     s.c_str(), ssidLen - 1); ssid[ssidLen - 1]     = '\0';
    strncpy(password, p.c_str(), passLen - 1); password[passLen - 1] = '\0';
}

bool WiFiManager::hasCredentials() {
    prefs.begin("wifi", false);
    bool has = prefs.isKey("ssid") && prefs.getString("ssid", "").length() > 0;
    prefs.end();
    return has;
}

String WiFiManager::getSavedSSID() {
    prefs.begin("wifi", false);
    String s = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
    prefs.end();
    return s;
}

bool WiFiManager::autoConnect(uint32_t timeoutMs) {
    char ssid[64] = {}, pass[64] = {};
    loadCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] == '\0') return false;

    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    Serial.printf("[WiFi] Auto-connect to saved SSID: %s\n", ssid);
    Serial.flush();

    uint32_t start = millis();
    wl_status_t lastStatus = WL_IDLE_STATUS;
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        wl_status_t status = WiFi.status();
        if (status != lastStatus) {
            Serial.printf("[WiFi] Auto-connect status: %d\n", status);
            Serial.flush();
            lastStatus = status;
        }
        // WL_CONNECT_FAILED: router rejected the attempt (wrong password, RF issue,
        // or router busy).  Retry up to 3 times.
        // IMPORTANT: WiFi.disconnect() returns immediately but the internal ESP-IDF
        // state machine needs time to fully reset before the next WiFi.begin() can
        // succeed — calling begin() too early causes ESP_ERR_WIFI_STATE (0x3006).
        // Wait until status becomes WL_DISCONNECTED (6) before retrying.
        if (status == WL_CONNECT_FAILED && retries < 3) {
            retries++;
            Serial.printf("[WiFi] Connection failed, retry %d/3 – waiting for clean disconnect...\n", retries);
            Serial.flush();
            WiFi.disconnect(false, false);

            // Wait up to 3 s for the WiFi stack to fully settle into DISCONNECTED state
            uint32_t discStart = millis();
            while (millis() - discStart < 3000) {
                wl_status_t s = WiFi.status();
                if (s == WL_DISCONNECTED || s == WL_IDLE_STATUS || s == WL_NO_SSID_AVAIL) break;
                delay(50);
            }
            delay(300); // extra margin for the internal state machine

            Serial.printf("[WiFi] Retry %d/3 – calling begin...\n", retries);
            Serial.flush();
            WiFi.begin(ssid, pass[0] ? pass : nullptr);
            lastStatus = WL_IDLE_STATUS;
        }
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        current_ssid = ssid;
        return true;
    }
    Serial.printf("[WiFi] Auto-connect timed out, final status: %d\n", WiFi.status());
    WiFi.disconnect(false, false);
    Serial.flush();
    return false;
}

void WiFiManager::stopAP() {
    WiFi.softAPdisconnect(true);
    // Switch to STA-only if station is up, otherwise turn radio off.
    WiFi.mode(isConnected() ? WIFI_MODE_STA : WIFI_MODE_NULL);
    current_mode = isConnected() ? MODE_STATION : MODE_OFF;
    Serial.println("[WiFi] AP stopped");
    Serial.flush();
}

bool WiFiManager::isAPActive() {
    wifi_mode_t m = WiFi.getMode();
    return (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);
}

void WiFiManager::startAP(const char *ssid, const char *password) {
    // Bring up APSTA so the station connection (if any) is preserved.
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.setSleep(false);
    current_ssid = ssid ? ssid : AP_SSID;
    bool ok = WiFi.softAP(current_ssid.c_str(), password ? password : AP_PASSWORD);
    current_mode = isConnected() ? MODE_AP_STA : MODE_AP;
    Serial.printf("[WiFi] AP %s: %s\n", ok ? "started" : "FAILED", current_ssid.c_str());
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.flush();
}

void WiFiManager::connectToWiFi(const char *ssid, const char *password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        Serial.println("[WiFi] Missing SSID, staying in AP mode");
        Serial.flush();
        return;
    }
    // Keep AP alive during connection attempt so the setup page stays
    // reachable if the credentials are wrong.
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.setSleep(false);
    WiFi.scanDelete();
    current_ssid = ssid;
    WiFi.begin(ssid, password && password[0] ? password : nullptr);
    current_mode = MODE_AP_STA;
    Serial.printf("[WiFi] Connecting to: %s\n", current_ssid.c_str());
    Serial.flush();
    // The caller (web handler) will return 200 immediately; the AP will be
    // stopped by App::loop() once WiFi.status() == WL_CONNECTED.
}

int WiFiManager::scanNetworks(bool includeHidden) {
    // Do NOT force WIFI_MODE_APSTA here – that would re-enable the AP even
    // after it was intentionally stopped.  Scanning works in STA mode too.
    WiFi.setSleep(false);

    Serial.printf("[WiFiScan] mode=%d status=%d AP-IP=%s STA-IP=%s\n",
                  WiFi.getMode(), WiFi.status(),
                  WiFi.softAPIP().toString().c_str(),
                  WiFi.localIP().toString().c_str());
    Serial.println("[WiFiScan] Cleaning old results and starting blocking scan");
    Serial.flush();

    WiFi.scanDelete();
    int n = WiFi.scanNetworks(false, includeHidden);

    if (n == WIFI_SCAN_FAILED) {
        Serial.println("[WiFiScan] Scan failed (WIFI_SCAN_FAILED)");
    } else if (n == WIFI_SCAN_RUNNING) {
        Serial.println("[WiFiScan] Scan still running unexpectedly");
    } else {
        Serial.printf("[WiFiScan] Found %d network(s)\n", n);
        for (int i = 0; i < n && i < 10; i++) {
            Serial.printf("[WiFiScan]   #%d SSID='%s' RSSI=%d channel=%d enc=%d\n",
                          i, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                          WiFi.channel(i), WiFi.encryptionType(i));
        }
    }
    Serial.flush();
    return n;
}

void WiFiManager::scan() {
    int n = scanNetworks(true);
    for (int i = 0; i < n; i++) {
        Serial.println(WiFi.SSID(i));
    }
    Serial.flush();
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
    WiFi.disconnect(true, false);
    current_mode = MODE_OFF;
}

WiFiMode WiFiManager::getCurrentMode() {
    return current_mode;
}
