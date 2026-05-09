#include "display.h"

#include <cstring>

namespace {
constexpr uint8_t LANDSCAPE_ROTATION = 1;
constexpr int SCREEN_W = DISPLAY_LANDSCAPE_WIDTH;
constexpr int SCREEN_H = DISPLAY_LANDSCAPE_HEIGHT;
constexpr int BUTTON_Y = 264;
constexpr int BUTTON_H = 42;
constexpr int BTN_REFRESH_X = 14;
constexpr int BTN_AP_X = 170;
constexpr int BTN_SCANNER_X = 326;
constexpr int BUTTON_W = 140;
}

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
    tft->setRotation(LANDSCAPE_ROTATION);
    tft->fillScreen(TFT_BLACK);
    tft->setTextFont(1);
    tft->setTextDatum(TL_DATUM);
    tft->setTextWrap(false, false);
    tft->setTextPadding(0);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    Serial.printf("[Display] Initialized ST7796 landscape %dx%d\n", tft->width(), tft->height());
}

void Display::fillScreen(uint16_t color) {
    if (tft != nullptr) tft->fillScreen(color);
}

void Display::drawText(int x, int y, const char *text, uint16_t color) {
    if (tft == nullptr) return;
    tft->setTextFont(1);
    tft->setTextColor(color, TFT_BLACK);
    tft->setCursor(x, y);
    tft->print(text);
}

void Display::clear() {
    if (tft != nullptr) tft->fillScreen(TFT_BLACK);
}

String Display::fitText(const String &text, uint8_t maxChars) const {
    if (text.length() <= maxChars) return text;
    if (maxChars <= 3) return text.substring(0, maxChars);
    return text.substring(0, maxChars - 3) + "...";
}

void Display::drawCard(int x, int y, int w, int h, const char *title, uint16_t accent) {
    tft->fillRoundRect(x, y, w, h, 10, TFT_DARKGREY);
    tft->drawRoundRect(x, y, w, h, 10, accent);
    tft->fillRoundRect(x, y, w, 28, 10, accent);
    tft->setTextFont(1);
    tft->setTextSize(2);
    tft->setTextColor(TFT_BLACK, accent);
    tft->setCursor(x + 10, y + 7);
    tft->print(title);
}

void Display::drawButton(int x, int y, int w, int h, const char *label, uint16_t color) {
    tft->fillRoundRect(x, y, w, h, 8, color);
    tft->drawRoundRect(x, y, w, h, 8, TFT_WHITE);
    tft->setTextSize(2);
    tft->setTextColor(TFT_BLACK, color);
    int16_t tx = x + (w - static_cast<int>(strlen(label)) * 12) / 2;
    if (tx < x + 8) tx = x + 8;
    tft->setCursor(tx, y + 14);
    tft->print(label);
}

void Display::showSplash() {
    if (tft == nullptr) return;
    tft->fillScreen(TFT_BLACK);

    tft->fillRoundRect(24, 34, SCREEN_W - 48, 180, 16, TFT_DARKGREY);
    tft->drawRoundRect(24, 34, SCREEN_W - 48, 180, 16, TFT_GREEN);

    tft->setTextFont(1);
    tft->setTextSize(4);
    tft->setTextColor(TFT_GREEN, TFT_DARKGREY);
    tft->setCursor(78, 78);
    tft->print("Lebensmittel");
    tft->setCursor(126, 126);
    tft->print("Scanner");

    tft->setTextSize(2);
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->setCursor(150, 242);
    tft->print("System startet...");
}

void Display::showWiFiStatus(const String &ssid, const String &ip, bool connected) {
    showDashboard(ssid, ip, connected, "", "", "", "", connected ? "WLAN verbunden" : "AP/Setup aktiv");
}

void Display::showDashboard(const String &ssid,
                            const String &ip,
                            bool wifiConnected,
                            const String &scannerStatus,
                            const String &scannerName,
                            const String &lastScan,
                            const String &lastType,
                            const String &message) {
    if (tft == nullptr) return;
    tft->fillScreen(TFT_BLACK);

    tft->fillRect(0, 0, SCREEN_W, 36, TFT_NAVY);
    tft->setTextFont(1);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, TFT_NAVY);
    tft->setCursor(14, 11);
    tft->print("Lebensmittel-Scanner");
    tft->setTextColor(TFT_YELLOW, TFT_NAVY);
    tft->setCursor(314, 11);
    tft->print("Querformat");

    drawCard(14, 50, 218, 92, "WLAN", wifiConnected ? TFT_GREEN : TFT_ORANGE);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->setTextSize(2);
    tft->setCursor(28, 88);
    tft->print(wifiConnected ? "Verbunden" : "Setup/AP aktiv");
    tft->setCursor(28, 114);
    tft->print(fitText(wifiConnected ? ssid : String(AP_SSID), 17));
    tft->setCursor(28, 136);
    tft->print(fitText(ip.isEmpty() ? String("192.168.4.1") : ip, 17));

    const bool scannerConnected = scannerStatus == "connected";
    const bool scannerBusy = scannerStatus == "connecting" || scannerStatus == "reconnecting";
    uint16_t scannerColor = scannerConnected ? TFT_GREEN : (scannerBusy ? TFT_YELLOW : TFT_RED);
    drawCard(248, 50, 218, 92, "Scanner", scannerColor);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->setCursor(262, 88);
    tft->print(fitText(scannerStatus.isEmpty() ? String("nicht gestartet") : scannerStatus, 17));
    tft->setCursor(262, 114);
    tft->print(fitText(scannerName.isEmpty() ? String("kein Geraet") : scannerName, 17));

    drawCard(14, 158, 452, 88, "Letzter Scan", lastScan.isEmpty() ? TFT_CYAN : TFT_GREEN);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->setCursor(28, 196);
    tft->setTextSize(2);
    tft->print(lastScan.isEmpty() ? "Noch kein Barcode gescannt" : fitText(lastScan, 34));
    tft->setCursor(28, 222);
    tft->setTextColor(TFT_CYAN, TFT_DARKGREY);
    tft->print(lastType.isEmpty() ? "Bereit fuer BLE-HID Scanner" : fitText(lastType, 34));

    if (!message.isEmpty()) {
        tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft->setCursor(14, 248);
        tft->print(fitText(message, 38));
    }

    drawButton(BTN_REFRESH_X, BUTTON_Y, BUTTON_W, BUTTON_H, "Aktual.", TFT_CYAN);
    drawButton(BTN_AP_X, BUTTON_Y, BUTTON_W, BUTTON_H, "Setup AP", TFT_ORANGE);
    drawButton(BTN_SCANNER_X, BUTTON_Y, BUTTON_W, BUTTON_H, "Scanner", TFT_GREEN);
}

OnscreenAction Display::hitTest(uint16_t x, uint16_t y) const {
    if (y < BUTTON_Y || y > BUTTON_Y + BUTTON_H) return OnscreenAction::NONE;
    if (x >= BTN_REFRESH_X && x <= BTN_REFRESH_X + BUTTON_W) return OnscreenAction::REFRESH;
    if (x >= BTN_AP_X && x <= BTN_AP_X + BUTTON_W) return OnscreenAction::START_AP;
    if (x >= BTN_SCANNER_X && x <= BTN_SCANNER_X + BUTTON_W) return OnscreenAction::SCANNER_RECONNECT;
    return OnscreenAction::NONE;
}
