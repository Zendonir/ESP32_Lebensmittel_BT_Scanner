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
    SWIPE_LEFT,   // next tab / swipe-to-delete
    SWIPE_RIGHT,  // prev tab
    SWIPE_UP,     // scroll up (inventory)
    SWIPE_DOWN,   // scroll down (inventory) / open location select
    // General
    REFRESH,
    WIFI_SETUP,
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
    // Inventory search bar tap
    INV_SEARCH,
    // Inventory column header tap → cycle sort mode (MHD / Name / Lagerort)
    INV_SORT,
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
    KB_SUGGEST,    // tap input bar to accept suggestion
    // Location badge (tapping the top-right location pill opens location select)
    LOCATION_BADGE,
    // Result screen — label print count
    PRINT_LABEL_1,
    PRINT_LABEL_2,
    PRINT_LABEL_3,
    PRINT_LABEL_5,
    PRINT_LABEL_10,
    // SD backup import prompt
    SD_IMPORT_YES,
    SD_IMPORT_NO,
    // FIFO out-of-order warning confirmation
    FIFO_CONFIRM,
    FIFO_CANCEL,
    // Generic confirm dialog (WiFi password etc.)
    CONFIRM_YES,
    CONFIRM_NO,
    // Label roll management
    NEW_ROLL,
    // OTA firmware update (tapping button on System tab)
    OTA_UPDATE,
    // Inventory date filters (tapping stat cards on home screen)
    INV_EXPIRING_7,   // show only items expiring within 7 days
    INV_EXPIRING_3,   // show only items expiring within 3 days (critical)
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
                  size_t inventoryCount, int expiringSoon,
                  const String &message = "",
                  bool sdMounted = false,
                  int rollRemaining = -1,
                  int scannerBattery = -1,
                  int expiringSoon3 = 0);

    // Barcode workflow
    void showFetchingProduct(const String &barcode);
    void showDateEntry(const ProductInfo &product, const String &dateDraft);
    void showQuantityEntry(const ProductInfo &product, const String &expiryDate, int quantity);
    void showResult(const String &title, const String &message, bool success, bool showPrintButtons = false);
    void showOtaStatus(const String &phase, int pct, const String &targetVersion);
    void showInventoryList(const std::vector<InventoryItem> &items,
                           const String &filter = "",
                           const String &hhAbbr = "",
                           const String &expandedGroup = "",
                           int scrollOffset = 0,
                           int sortMode = 0);   // 0=MHD, 1=Name, 2=Lagerort

    int16_t getLastSwipePressY()    const;  // Y coordinate of last swipe press start
    int16_t getScrollDeltaPx()      const;  // live vertical drag delta (0 if not scrolling)
    int16_t drainScrollCommit()     const;  // final delta after finger lift, clears on read
    int     getScrollableCount()     const; // item/group count from last scrollable render

    String getInvGroupName(int rowIdx) const;

    void showInventoryGroupDetail(const String &groupName,
                                  const std::vector<InventoryItem> &groupItems,
                                  const String &hhAbbr = "");

    // Template workflow
    void showCategoryTiles(const std::vector<String> &categories);
    void showListScreen(const char *title, const std::vector<String> &items,
                        int scrollOffset = 0);
    void showTemplateMHD(const String &productName, const String &mhd);
    void showAmountEntry(const String &productName, const String &unit, const String &draft);
    void showNewRollEntry(const String &draft);

    // Location
    void setActiveLocation(const String &loc);
    void setActiveLocationColor(const String &hexColor);
    void showLocationSelect(const String &current, const std::vector<String> &locations);

    // WiFi setup scan screen
    void showWifiScan();

    // SD backup import prompt
    void showSdImportPrompt(const String &backupDate);
    void showConfirmDialog(const String &title, const String &body);

    // FIFO out-of-order warning (asks Ja/Nein before removing)
    void showFifoWarning(const String &productName, const String &olderExpiry);

    // Keyboard entry
    void showKeyboardEntry(const String &title, const String &current, const String &suggestion = "");
    void showSearchEntry(const String &current, const String &suggestion);
    char drainKbChar();  // returns pending keyboard char, 0 if none
    void kbAutoShift(char lastChar);   // call after each typed char
    void kbReset();                    // call when opening fresh keyboard
    void kbToggleCaps();               // handle KB_CAPS action

    // Action queue
    OnscreenAction hitTest(uint16_t x, uint16_t y) const;

    // Touch state query — used by App::loop() for display wakeup on finger-down
    bool isTouchActive() const;

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
