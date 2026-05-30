#include "wifi_manager.h"
#include <Preferences.h>

WiFiManager wifi_manager;

WiFiManager::WiFiManager() : current_mode(MODE_OFF) {}

void WiFiManager::init() {
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_MODE_STA);
    if (hasCredentials()) {
        if (autoConnect(15000)) {
            Serial.printf("[WiFi] Station connected, IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[WiFi] Auto-connect failed");
        }
    } else {
        Serial.println("[WiFi] No saved credentials");
    }
    current_ssid = isConnected() ? WiFi.SSID() : "";
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

    WiFi.mode(WIFI_MODE_STA);
    WiFi.setSleep(false);
    // Disable auto-reconnect so the ESP-IDF WiFi stack doesn't start its own
    // reconnect attempt between our retries, which would cause ESP_ERR_WIFI_STATE
    // when we call WiFi.begin() while the stack is already connecting.
    WiFi.setAutoReconnect(false);
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    Serial.printf("[WiFi] Auto-connect to saved SSID: %s\n", ssid);
    Serial.flush();

    uint32_t start = millis();
    wl_status_t lastStatus = WL_IDLE_STATUS;
    int retries = 0;
    uint32_t stuckSince = millis();   // tracks when we last saw a status we wanted to leave
    wl_status_t stuckStatus = WL_DISCONNECTED;

    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        wl_status_t status = WiFi.status();
        if (status != lastStatus) {
            Serial.printf("[WiFi] Auto-connect status: %d\n", status);
            Serial.flush();
            lastStatus = status;
            // Reset stuck timer on any status change
            stuckSince  = millis();
            stuckStatus = status;
        }

        // WL_CONNECT_FAILED: router rejected the attempt (wrong password, RF issue,
        // or router busy).  Retry up to 3 times.
        if (status == WL_CONNECT_FAILED && retries < 6) {
            retries++;
            Serial.printf("[WiFi] Connection failed, retry %d/6 – waiting for clean disconnect...\n", retries);
            Serial.flush();
            WiFi.disconnect(false, false);

            // Wait up to 3 s for the WiFi stack to reach WL_DISCONNECTED
            uint32_t discStart = millis();
            while (millis() - discStart < 3000) {
                wl_status_t s = WiFi.status();
                if (s == WL_DISCONNECTED || s == WL_NO_SSID_AVAIL) break;
                delay(50);
            }
            delay(300); // extra margin for the internal state machine

            Serial.printf("[WiFi] Retry %d/6 – calling begin...\n", retries);
            Serial.flush();
            WiFi.begin(ssid, pass[0] ? pass : nullptr);
            lastStatus = WL_IDLE_STATUS;
            stuckSince = millis();
        }

        // WL_DISCONNECTED held for >8 s without any progress means the stack
        // silently failed to start the connection (common when the AP is on a
        // crowded channel or when APSTA mode delays the STA associate).
        // Treat it like a connect failure and retry.
        if ((status == WL_DISCONNECTED || status == WL_IDLE_STATUS)
                && retries < 6
                && millis() - stuckSince > 8000) {
            retries++;
            Serial.printf("[WiFi] Stuck at status %d for 8s, retry %d/6\n", status, retries);
            Serial.flush();
            WiFi.disconnect(false, false);
            delay(500);
            WiFi.begin(ssid, pass[0] ? pass : nullptr);
            lastStatus = WL_IDLE_STATUS;
            stuckSince = millis();
        }

        delay(250);
    }
    // Re-enable auto-reconnect so the station can recover from transient drops
    // during normal operation without requiring a full restart.
    WiFi.setAutoReconnect(true);
    if (WiFi.status() == WL_CONNECTED) {
        current_ssid = ssid;
        return true;
    }
    Serial.printf("[WiFi] Auto-connect timed out, final status: %d\n", WiFi.status());
    WiFi.disconnect(false, false);
    Serial.flush();
    return false;
}

void WiFiManager::connectToWiFi(const char *ssid, const char *password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        Serial.println("[WiFi] Missing SSID, aborting connect");
        Serial.flush();
        return;
    }
    WiFi.mode(WIFI_MODE_STA);
    WiFi.setSleep(false);
    WiFi.scanDelete();
    current_ssid = ssid;
    WiFi.begin(ssid, password && password[0] ? password : nullptr);
    current_mode = MODE_STATION;
    Serial.printf("[WiFi] Connecting to: %s\n", current_ssid.c_str());
    Serial.flush();
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
    return current_ssid;
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true, false);
    current_mode = MODE_OFF;
}

void WiFiManager::reconnect(bool hardReset) {
    char ssid[64] = {}, pass[64] = {};
    loadCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] == '\0') return;

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);  // we drive reconnection manually
    WiFi.setSleep(false);

    if (hardReset) {
        // Soft reconnect (disconnect+begin) can get stuck on IDF 5.x: disconnect()
        // is asynchronous, so an immediate begin() may be rejected with
        // ESP_ERR_WIFI_STATE and never actually associate — the exact state a
        // device power-cycle recovers from.  Fully re-initialise the WiFi driver
        // instead.  This touches ONLY the WiFi driver, not the BLE controller
        // (NimBLE runs on the separate Bluetooth controller and is unaffected).
        WiFi.disconnect(true, false);   // wifioff=true → tear the radio down
        WiFi.mode(WIFI_MODE_NULL);      // release the WiFi driver completely
        WiFi.mode(WIFI_MODE_STA);       // bring it back up clean
        Serial.println("[WiFi] Hard reset of WiFi driver (BLE untouched)");
    }
    // No disconnect() before begin() on the soft path: after an AP-side drop the
    // stack is already in WL_DISCONNECTED, so begin() associates immediately —
    // mirroring the working initial autoConnect() path.
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    current_ssid = ssid;
    current_mode = MODE_STATION;
    Serial.printf("[WiFi] Reconnect%s started: %s\n", hardReset ? " (hard)" : "", ssid);
    Serial.flush();
}

WiFiMode WiFiManager::getCurrentMode() {
    return current_mode;
}
