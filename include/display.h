#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

enum class OnscreenAction {
    NONE,
    REFRESH,
    START_AP,
    SCANNER_RECONNECT
};

class Display {
public:
    Display();
    ~Display();
    void init();
    void drawText(int x, int y, const char *text, uint16_t color = TFT_WHITE);
    void fillScreen(uint16_t color);
    void showSplash();
    void showWiFiStatus(const String &ssid, const String &ip, bool connected);
    void showDashboard(const String &ssid,
                       const String &ip,
                       bool wifiConnected,
                       const String &scannerStatus,
                       const String &scannerName,
                       const String &lastScan,
                       const String &lastType,
                       const String &message = "");
    OnscreenAction hitTest(uint16_t x, uint16_t y) const;
    void clear();

private:
    TFT_eSPI *tft;

    void drawButton(int x, int y, int w, int h, const char *label, uint16_t color);
    void drawCard(int x, int y, int w, int h, const char *title, uint16_t accent);
    String fitText(const String &text, uint8_t maxChars) const;
};

extern Display display_obj;

#endif
