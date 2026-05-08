#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "audio.h"
#include "wifi_manager.h"
#include "touch.h"

TwoWire i2c_bus(0);

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== ESP32-S3 Scanner Board ===\n");

    // Initialize I2C Bus
    Serial.println("[I2C] Initializing...");
    i2c_bus.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);

    // Initialize Touch
    Serial.println("[Touch] Initializing...");
    touch_obj.init(&i2c_bus);

    // Initialize Audio
    Serial.println("[Audio] Initializing...");
    audio_obj.init();
    audio_obj.playTone(1000, 200);

    // Initialize WiFi
    Serial.println("[WiFi] Initializing...");
    wifi_manager.init();

    Serial.println("\n=== Ready ===\n");
    Serial.println("Commands: scan, ap, status, beep, help\n");
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd == "scan") {
            Serial.println("[Command] Scanning WiFi...");
            wifi_manager.scan();
        } else if (cmd == "ap") {
            Serial.println("[Command] Starting AP Mode...");
            wifi_manager.startAP();
        } else if (cmd == "status") {
            Serial.println("\n=== System Status ===");
            Serial.print("WiFi: ");
            Serial.println(wifi_manager.isConnected() ? "Connected" : "Offline");
            Serial.print("IP: ");
            Serial.println(wifi_manager.getIPAddress());
            Serial.print("\n");
        } else if (cmd == "beep") {
            audio_obj.playTone(1000, 100);
            Serial.println("Beep!");
        } else if (cmd == "touch") {
            TouchPoint p = touch_obj.read();
            Serial.print("Touch: ");
            Serial.print(p.x);
            Serial.print(" ");
            Serial.println(p.y);
        } else if (cmd == "help") {
            Serial.println("\n=== Commands ===");
            Serial.println("scan    - Scan WiFi");
            Serial.println("ap      - AP Mode");
            Serial.println("status  - Status");
            Serial.println("beep    - Beep");
            Serial.println("touch   - Read touch");
            Serial.println("help    - Help\n");
        } else if (!cmd.isEmpty()) {
            Serial.println("Unknown command");
        }
    }
    delay(100);
}
