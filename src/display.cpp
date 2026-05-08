#include "display.h"

Display display_obj;

Display::Display() : tft(nullptr) {}

Display::~Display() {
    if (tft != nullptr) {
        delete tft;
    }
}

void Display::init() {
    if (tft == nullptr) {
        tft = new TFT_eSPI();
    }
    tft->init();
    tft->setRotation(0);
    tft->fillScreen(TFT_BLACK);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE);
    Serial.println("[Display] Initialized ST7796 320x480");
}

void Display::fillScreen(uint16_t color) {
    if (tft != nullptr) tft->fillScreen(color);
}

void Display::drawText(int x, int y, const char *text, uint16_t color) {
    if (tft == nullptr) return;
    tft->setTextColor(color);
    tft->setCursor(x, y);
    tft->print(text);
}

void Display::clear() {
    if (tft != nullptr) tft->fillScreen(TFT_BLACK);
}

void Display::showSplash() {
    if (tft == nullptr) return;
    tft->fillScreen(TFT_BLACK);
    tft->setTextSize(3);
    tft->setTextColor(TFT_GREEN);
    tft->setCursor(30, 100);
    tft->print("ESP32-S3");
    tft->setCursor(40, 150);
    tft->print("Scanner");

    tft->setTextSize(2);
    tft->setTextColor(TFT_CYAN);
    tft->setCursor(50, 250);
    tft->print("Initializing...");

    delay(2000);
}

void Display::showWiFiStatus(const char *ssid, const char *ip, bool connected) {
    if (tft == nullptr) return;
    tft->fillScreen(TFT_BLACK);

    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE);
    tft->setCursor(20, 50);
    tft->print("WiFi Status");

    tft->setTextSize(1);
    tft->setCursor(20, 100);
    tft->print("Status: ");
    tft->setTextColor(connected ? TFT_GREEN : TFT_RED);
    tft->print(connected ? "Connected" : "Disconnected");

    tft->setTextColor(TFT_WHITE);
    tft->setCursor(20, 130);
    tft->print("SSID: ");
    tft->setTextColor(TFT_CYAN);
    tft->print(ssid ? ssid : "None");

    tft->setTextColor(TFT_WHITE);
    tft->setCursor(20, 160);
    tft->print("IP: ");
    tft->setTextColor(TFT_YELLOW);
    tft->print(ip ? ip : "0.0.0.0");

    tft->setTextColor(TFT_WHITE);
    tft->setTextSize(1);
    tft->setCursor(20, 220);
    tft->print("AP: 192.168.4.1");
    tft->setCursor(20, 240);
    tft->print("Press BOOT to refresh");
}
