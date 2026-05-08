#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "config.h"
#include "wifi_manager.h"

AsyncWebServer server(80);

void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><head><title>ESP32 Scanner</title></head><body>";
        html += "<h1>ESP32-S3 Scanner</h1>";
        html += "<p>WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Offline") + "</p>";
        html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
        html += "</body></html>";
        request->send(200, "text/html", html);
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + "}";
        request->send(200, "application/json", json);
    });

    server.begin();
    Serial.println("[Server] Web Server started on 192.168.4.1");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== ESP32-S3 Scanner ===\n");

    // WiFi
    Serial.println("[WiFi] Initializing...");
    wifi_manager.init();

    // Web Server
    setupWebServer();

    Serial.println("\n=== Ready ===");
    Serial.println("AP: 192.168.4.1");
    Serial.println("SSID: ESP32-Scanner");
    Serial.println("Password: 12345678\n");
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd == "status") {
            Serial.println("WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Offline"));
            Serial.println("IP: " + WiFi.localIP().toString());
        } else if (cmd == "scan") {
            Serial.println("[WiFi] Scanning...");
            int n = WiFi.scanNetworks();
            Serial.print("Found ");
            Serial.print(n);
            Serial.println(" networks");
            for (int i = 0; i < n; i++) {
                Serial.print(i + 1);
                Serial.print(": ");
                Serial.println(WiFi.SSID(i));
            }
        } else if (!cmd.isEmpty()) {
            Serial.println("Commands: status, scan");
        }
    }
    delay(100);
}
