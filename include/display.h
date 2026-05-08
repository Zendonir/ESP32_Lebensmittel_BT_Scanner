#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

class Display {
public:
    Display();
    ~Display();
    void init();
    void drawText(int x, int y, const char *text, uint16_t color = TFT_WHITE);
    void fillScreen(uint16_t color);
    void showSplash();
    void showWiFiStatus(const char *ssid, const char *ip, bool connected);
    void clear();

private:
    TFT_eSPI *tft;
};

extern Display display_obj;

#endif
