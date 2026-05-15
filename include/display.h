#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <vector>
#include "config.h"

struct InventoryItem;
struct ProductInfo;

enum class OnscreenAction {
    NONE,
    // Tab navigation
    TAB_STORE,
    TAB_INVENTORY,
    TAB_SCANNER,
    TAB_SYSTEM,
    TAB_MANUAL_PRODUCT,
    TAB_MANUAL_ENTRY,
    // Swipe gestures
    SWIPE_LEFT,   // next tab
    SWIPE_RIGHT,  // prev tab
    // General
    REFRESH,
    START_AP,
    SCANNER_RECONNECT,
    CANCEL,
    // Date numpad
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
    // Quantity entry — direct selection buttons 1..12
    QTY_1, QTY_2, QTY_3, QTY_4, QTY_5, QTY_6,
    QTY_7, QTY_8, QTY_9, QTY_10, QTY_11, QTY_12,
    QTY_CONFIRM,
    // Legacy (kept so old references compile; no longer drawn)
    QTY_MINUS,
    QTY_PLUS,
    // Printer
    PRINTER_FEED_1,
    PRINTER_FEED_5,
    // Template list selection (up to 7 visible items)
    LIST_ITEM_0,
    LIST_ITEM_1,
    LIST_ITEM_2,
    LIST_ITEM_3,
    LIST_ITEM_4,
    LIST_ITEM_5,
    LIST_ITEM_6,
    // Template MHD adjust
    MHD_DAY_MINUS,
    MHD_DAY_PLUS,
    MHD_CONFIRM,
    // On-screen keyboard
    KB_CHAR,       // char in _pending_kb_char
    KB_BACKSPACE,
    KB_CAPS,       // toggle caps-lock
    KB_CONFIRM,
};

enum class UiTab {
    STORE,
    INVENTORY,
    SCANNER,
    SYSTEM,
    MANUAL_PRODUCT,
    MANUAL_ENTRY,
};

class Display {
public:
    Display();
    ~Display();

    void init();
    void tick();
    void startTouchTask();

    // Home screens
    void showSplash();
    void showWiFiStatus(const String &ssid, const String &ip, bool connected);
    void showDashboard(const String &ssid, const String &ip, bool wifiConnected,
                       const String &scannerStatus, const String &scannerName,
                       const String &lastScan, const String &lastType,
                       const String &message = "");
    void showHome(UiTab activeTab,
                  const String &ssid, const String &ip, bool wifiConnected,
                  const String &scannerStatus, const String &scannerName,
                  const String &lastScan, const String &lastType,
                  size_t inventoryCount, const String &message = "");

    // Barcode workflow
    void showFetchingProduct(const String &barcode);
    void showDateEntry(const ProductInfo &product, const String &dateDraft);
    void showQuantityEntry(const ProductInfo &product, const String &expiryDate, int quantity);
    void showResult(const String &title, const String &message, bool success);
    void showInventoryList(const std::vector<InventoryItem> &items);

    // Template workflow
    void showCategoryTiles(const std::vector<String> &categories);
    void showListScreen(const char *title, const std::vector<String> &items, bool showBack);
    void showTemplateMHD(const String &productName, const String &mhd, int qty);

    // Keyboard entry
    void showKeyboardEntry(const String &title, const String &current);
    char drainKbChar();  // returns pending keyboard char, 0 if none
    void kbAutoShift(char lastChar);   // call after each typed char
    void kbReset();                    // call when opening fresh keyboard
    void kbToggleCaps();               // handle KB_CAPS action

    // Action queue
    OnscreenAction hitTest(uint16_t x, uint16_t y) const;

    // Legacy helpers
    void drawText(int x, int y, const char *text, uint16_t color = 0xFFFF);
    void fillScreen(uint16_t color);
    void clear();

private:
    bool _initialized = false;
    bool _kbShift     = true;   // next key is uppercase (auto-shift or caps)
    bool _kbCaps      = false;  // caps-lock is latched on
};

extern Display display_obj;

#endif
