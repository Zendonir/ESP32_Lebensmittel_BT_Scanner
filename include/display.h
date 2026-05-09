#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
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
    QTY_CONFIRM
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
    OnscreenAction hitTest(uint16_t x, uint16_t y) const;
    void clear();

private:
    enum class ScreenKind {
        HOME,
        DATE_ENTRY,
        QUANTITY_ENTRY,
        MESSAGE,
        INVENTORY_LIST
    } currentScreen = ScreenKind::HOME;

    TFT_eSPI *tft;

    void drawHeader(UiTab activeTab, const char *title);
    void drawTab(int x, const char *label, bool active, uint16_t accent);
    void drawButton(int x, int y, int w, int h, const char *label, uint16_t color, uint16_t textColor = TFT_BLACK);
    void drawCard(int x, int y, int w, int h, const char *title, uint16_t accent);
    void drawStatusPill(int x, int y, const char *label, uint16_t color);
    void drawKeypad(const String &value, bool dateMode);
    String fitText(const String &text, uint8_t maxChars) const;
};

extern Display display_obj;

#endif
