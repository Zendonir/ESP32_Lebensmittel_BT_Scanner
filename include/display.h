#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <vector>
#include "config.h"

struct InventoryItem;
struct ProductInfo;

enum class OnscreenAction {
    NONE,
    TAB_STORE,
    TAB_INVENTORY,
    TAB_SCANNER,
    TAB_SYSTEM,
    REFRESH,
    START_AP,
    SCANNER_RECONNECT,
    CANCEL,
    DATE_DIGIT_0,
    DATE_DIGIT_1,
    DATE_DIGIT_2,
    DATE_DIGIT_3,
    DATE_DIGIT_4,
    DATE_DIGIT_5,
    DATE_DIGIT_6,
    DATE_DIGIT_7,
    DATE_DIGIT_8,
    DATE_DIGIT_9,
    DATE_BACKSPACE,
    DATE_CONFIRM,
    QTY_MINUS,
    QTY_PLUS,
    QTY_CONFIRM,
    PRINTER_FEED_1,   // feed 1 line (manual paper positioning)
    PRINTER_FEED_5    // feed 5 lines
};

enum class UiTab {
    STORE,
    INVENTORY,
    SCANNER,
    SYSTEM
};

class Display {
public:
    Display();
    ~Display();

    void init();

    /* Called every loop() iteration – drives the LVGL timer/renderer */
    void tick();

    /* Screen transitions (same API as before, now backed by LVGL) */
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
    void showHome(UiTab activeTab,
                  const String &ssid,
                  const String &ip,
                  bool wifiConnected,
                  const String &scannerStatus,
                  const String &scannerName,
                  const String &lastScan,
                  const String &lastType,
                  size_t inventoryCount,
                  const String &message = "");
    void showFetchingProduct(const String &barcode);
    void showDateEntry(const ProductInfo &product, const String &dateDraft);
    void showQuantityEntry(const ProductInfo &product, const String &expiryDate, int quantity);
    void showResult(const String &title, const String &message, bool success);
    void showInventoryList(const std::vector<InventoryItem> &items);

    /* hitTest() – returns (and clears) the most recent action queued by
       LVGL button callbacks.  x/y are unused; LVGL resolves them internally. */
    OnscreenAction hitTest(uint16_t x, uint16_t y) const;

    /* Legacy helpers kept for compatibility */
    void drawText(int x, int y, const char *text, uint16_t color = 0xFFFF);
    void fillScreen(uint16_t color);
    void clear();

private:
    bool _initialized = false;
};

extern Display display_obj;

#endif
