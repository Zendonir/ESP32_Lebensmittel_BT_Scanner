#include <Arduino.h>
#include <Wire.h>
#include <ESPAsyncWebServer.h>
#include "config.h"
#include "audio.h"
#include "wifi_manager.h"
#include "touch.h"

TwoWire i2c_bus(0);
AsyncWebServer server(80);

void initBacklight() {
    ledcAttach(LCD_BL, 5000, 8);
    ledcWrite(LCD_BL, 255);
    Serial.println("[Display] Backlight ON (GPIO 46)");
}

void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>ESP32 Scanner</title><style>";
        html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
        html += ".container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
        html += "h1 { color: #333; }";
        html += ".status { padding: 10px; margin: 10px 0; background: #e8f5e9; border-radius: 4px; }";
        html += "button { padding: 10px 20px; margin: 5px; background: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; }";
        html += "button:hover { background: #45a049; }";
        html += "input { width: 100%; padding: 8px; margin: 5px 0; box-sizing: border-box; }";
        html += "select { width: 100%; padding: 8px; margin: 5px 0; }";
        html += "</style></head><body><div class=\"container\">";
        html += "<h1>ESP32-S3 Scanner</h1>";
        html += "<div id=\"status\" class=\"status\">Loading...</div>";
        html += "<h2>WiFi Settings</h2>";
        html += "<label>Network:</label><select id=\"networks\"></select>";
        html += "<label>Password:</label><input type=\"password\" id=\"password\" placeholder=\"Enter WiFi password\">";
        html += "<button onclick=\"connect()\">Connect</button>";
        html += "<button onclick=\"scan()\">Scan Networks</button>";
        html += "<button onclick=\"apMode()\">Start AP Mode</button>";
        html += "<button onclick=\"beep()\">Beep</button>";
        html += "<button onclick=\"location.reload()\">Refresh</button>";
        html += "</div><script>";
        html += "function updateStatus(){fetch('/status').then(r=>r.json()).then(d=>{document.getElementById('status').innerHTML='<b>WiFi:</b> '+(d.connected?'Connected':'Offline')+'<br><b>SSID:</b> '+(d.ssid||'None')+'<br><b>IP:</b> '+d.ip;});}";
        html += "function scan(){fetch('/scan').then(()=>{setTimeout(()=>{fetch('/networks').then(r=>r.json()).then(nets=>{let sel=document.getElementById('networks');sel.innerHTML='';nets.forEach(n=>{let opt=document.createElement('option');opt.value=n;opt.text=n;sel.appendChild(opt);});});},2000);});}";
        html += "function connect(){let ssid=document.getElementById('networks').value;let pass=document.getElementById('password').value;fetch('/connect',{method:'POST',body:'ssid='+ssid+'&password='+pass});setTimeout(updateStatus,3000);}";
        html += "function apMode(){fetch('/ap').then(()=>{setTimeout(updateStatus,1000);});}";
        html += "function beep(){fetch('/beep');}";
        html += "updateStatus();setInterval(updateStatus,5000);";
        html += "</script></body></html>";
        request->send(200, "text/html", html);
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{\"connected\":" + String(wifi_manager.isConnected() ? "true" : "false") +
                      ",\"ssid\":\"" + wifi_manager.getSSID() +
                      "\",\"ip\":\"" + wifi_manager.getIPAddress() + "\"}";
        request->send(200, "application/json", json);
    });

    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        wifi_manager.scan();
        request->send(200, "text/plain", "Scanning...");
    });

    server.on("/networks", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "[";
        int n = WiFi.scanNetworks(false, false, false, 100);
        for (int i = 0; i < n && i < 10; i++) {
            if (i > 0) json += ",";
            json += "\"" + WiFi.SSID(i) + "\"";
        }
        json += "]";
        request->send(200, "application/json", json);
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
            String ssid = request->getParam("ssid", true)->value();
            String pass = request->getParam("password", true)->value();
            wifi_manager.connectToWiFi(ssid.c_str(), pass.c_str());
        }
        request->send(200, "text/plain", "Connecting...");
    });

    server.on("/ap", HTTP_GET, [](AsyncWebServerRequest *request) {
        wifi_manager.startAP();
        request->send(200, "text/plain", "AP Mode");
    });

    server.on("/beep", HTTP_GET, [](AsyncWebServerRequest *request) {
        audio_obj.playTone(1000, 100);
        request->send(200, "text/plain", "Beep!");
    });

    server.begin();
    Serial.println("[Server] Web Server started on 192.168.4.1");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== ESP32-S3 Scanner ===\n");

    // Backlight
    initBacklight();

    // I2C
    Serial.println("[I2C] Initializing...");
    i2c_bus.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);

    // Touch
    Serial.println("[Touch] Initializing...");
    touch_obj.init(&i2c_bus);

    // Audio
    Serial.println("[Audio] Initializing...");
    audio_obj.init();
    audio_obj.playTone(1000, 200);

    // WiFi
    Serial.println("[WiFi] Initializing...");
    wifi_manager.init();

    // Web Server
    setupWebServer();

    Serial.println("\n=== Ready ===");
    Serial.println("AP: 192.168.4.1");
    Serial.println("Commands: scan, ap, status, beep, help\n");
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd == "scan") {
            Serial.println("[CMD] Scanning WiFi...");
            wifi_manager.scan();
        } else if (cmd == "ap") {
            Serial.println("[CMD] AP Mode");
            wifi_manager.startAP();
        } else if (cmd == "status") {
            Serial.println("\n=== Status ===");
            Serial.print("WiFi: ");
            Serial.println(wifi_manager.isConnected() ? "Connected" : "Offline");
            Serial.print("IP: ");
            Serial.println(wifi_manager.getIPAddress());
            Serial.print("SSID: ");
            Serial.println(wifi_manager.getSSID());
            Serial.println();
        } else if (cmd == "beep") {
            audio_obj.playTone(1000, 100);
        } else if (cmd == "help") {
            Serial.println("\n=== Commands ===");
            Serial.println("scan   - Scan WiFi");
            Serial.println("ap     - AP Mode");
            Serial.println("status - Status");
            Serial.println("beep   - Beep\n");
        } else if (!cmd.isEmpty()) {
            Serial.println("Unknown command");
        }
    }
    delay(100);
}
