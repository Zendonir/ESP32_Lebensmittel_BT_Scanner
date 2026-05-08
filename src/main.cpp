#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "config.h"
#include "display.h"
#include "touch.h"
#include "audio.h"
#include "wifi_manager.h"
#include "ui.h"

// Global I2C instance for touch, IMU, RTC
TwoWire i2c_bus(0);

// Task handles
TaskHandle_t lvgl_task_handle = NULL;
TaskHandle_t touch_task_handle = NULL;
TaskHandle_t wifi_task_handle = NULL;

// Touch callback for LVGL
void touchInputCallback(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    TouchPoint point = touch_obj.read();
    data->point.x = point.x;
    data->point.y = point.y;
    data->state = point.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

// LVGL Update Task
void lvglTask(void *param) {
    const TickType_t xDelay = 10 / portTICK_PERIOD_MS; // 10ms

    while (1) {
        display_obj.update();
        vTaskDelay(xDelay);
    }
}

// Touch Task
void touchTask(void *param) {
    const TickType_t xDelay = 50 / portTICK_PERIOD_MS; // 50ms

    while (1) {
        TouchPoint point = touch_obj.read();
        if (point.pressed) {
            Serial.print("[Touch] X: ");
            Serial.print(point.x);
            Serial.print(" Y: ");
            Serial.println(point.y);
        }
        vTaskDelay(xDelay);
    }
}

// WiFi Task
void wifiTask(void *param) {
    const TickType_t xDelay = 5000 / portTICK_PERIOD_MS; // 5 seconds

    while (1) {
        bool connected = wifi_manager.isConnected();
        ui_obj.updateWiFiStatus(connected, wifi_manager.getSSID().c_str());
        vTaskDelay(xDelay);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== ESP32-S3 Scanner Board Initialization ===\n");

    // Initialize I2C Bus
    Serial.println("[I2C] Initializing I2C bus...");
    i2c_bus.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);

    // Initialize Display
    Serial.println("[Display] Initializing display...");
    display_obj.init();
    delay(500);

    // Initialize Touch
    Serial.println("[Touch] Initializing touch...");
    touch_obj.init(&i2c_bus);

    // Initialize Audio
    Serial.println("[Audio] Initializing audio...");
    audio_obj.init();
    audio_obj.playTone(1000, 200); // Startup sound

    // Initialize WiFi
    Serial.println("[WiFi] Initializing WiFi...");
    wifi_manager.init();

    // Initialize UI
    Serial.println("[UI] Creating interface...");
    ui_obj.init();
    ui_obj.updateWiFiStatus(false, "Not Connected");

    // Create FreeRTOS Tasks
    Serial.println("[Tasks] Creating FreeRTOS tasks...");

    xTaskCreatePinnedToCore(
        lvglTask,           // Task function
        "LVGL Update",      // Task name
        4096,               // Stack size
        NULL,               // Parameters
        2,                  // Priority
        &lvgl_task_handle,  // Task handle
        0                   // Core
    );

    xTaskCreatePinnedToCore(
        touchTask,
        "Touch Read",
        2048,
        NULL,
        3,
        &touch_task_handle,
        1
    );

    xTaskCreatePinnedToCore(
        wifiTask,
        "WiFi Update",
        4096,
        NULL,
        1,
        &wifi_task_handle,
        1
    );

    Serial.println("\n=== Initialization Complete ===\n");
    Serial.println("Board Status:");
    Serial.println("  - Display: Ready");
    Serial.println("  - Touch: Ready");
    Serial.println("  - Audio: Ready");
    Serial.println("  - WiFi: Ready (AP Mode active)");
    Serial.print("\n  Access Point: ");
    Serial.println(AP_SSID);
    Serial.print("  Password: ");
    Serial.println(AP_PASSWORD);
}

void loop() {
    // Main loop - most work done in FreeRTOS tasks
    delay(100);

    // Monitor serial input for commands
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();

        if (command == "scan") {
            Serial.println("[Command] Scanning WiFi networks...");
            wifi_manager.scan();
        } else if (command.startsWith("connect ")) {
            String ssid = command.substring(8);
            Serial.print("[Command] Connecting to: ");
            Serial.println(ssid);
            // In a real app, you'd prompt for password
        } else if (command == "ap") {
            Serial.println("[Command] Starting AP Mode...");
            wifi_manager.startAP();
        } else if (command == "status") {
            Serial.println("\n=== System Status ===");
            Serial.print("WiFi Connected: ");
            Serial.println(wifi_manager.isConnected() ? "Yes" : "No");
            Serial.print("IP Address: ");
            Serial.println(wifi_manager.getIPAddress());
            Serial.print("Mode: ");
            switch (wifi_manager.getCurrentMode()) {
                case MODE_OFF:
                    Serial.println("Off");
                    break;
                case MODE_STATION:
                    Serial.println("Station (WiFi Client)");
                    break;
                case MODE_AP:
                    Serial.println("Access Point");
                    break;
                case MODE_AP_STA:
                    Serial.println("Access Point + Station");
                    break;
            }
        } else if (command == "beep") {
            Serial.println("[Command] Playing beep...");
            audio_obj.playTone(1000, 100);
        } else if (command == "help") {
            Serial.println("\n=== Available Commands ===");
            Serial.println("  scan      - Scan WiFi networks");
            Serial.println("  ap        - Start Access Point");
            Serial.println("  status    - Show system status");
            Serial.println("  beep      - Play test tone");
            Serial.println("  help      - Show this help");
        } else if (!command.isEmpty()) {
            Serial.println("Unknown command. Type 'help' for available commands.");
        }
    }
}
