#include "display.h"

#include <cstring>
#include "models/InventoryItem.h"
#include "models/ProductInfo.h"

namespace {
constexpr uint8_t LANDSCAPE_ROTATION = 1;
constexpr int SCREEN_W = DISPLAY_LANDSCAPE_WIDTH;
constexpr int SCREEN_H = DISPLAY_LANDSCAPE_HEIGHT;
constexpr int HEADER_H = 48;
constexpr int TAB_Y = 8;
constexpr int TAB_H = 32;
constexpr int TAB_W = 108;
constexpr int TAB_GAP = 8;
constexpr int TAB_X0 = 14;
constexpr int BTN_Y = 264;
constexpr int BTN_H = 42;
constexpr int BTN_W = 140;
constexpr int BTN_REFRESH_X = 14;
constexpr int BTN_AP_X = 170;
constexpr int BTN_SCANNER_X = 326;
constexpr int KEY_X = 246;
constexpr int KEY_Y = 86;
constexpr int KEY_W = 66;
constexpr int KEY_H = 38;
constexpr int KEY_GAP = 8;
constexpr uint16_t COLOR_BG = 0x080D;       // near-black blue
constexpr uint16_t COLOR_HEADER = 0x118F;   // deep slate
constexpr uint16_t COLOR_SURFACE = 0x2149;  // soft graphite
constexpr uint16_t COLOR_SURFACE_2 = 0x3A30;
constexpr uint16_t COLOR_ACCENT = 0x5D9F;    // desaturated cyan
constexpr uint16_t COLOR_WARN = 0xFDEB;
constexpr uint16_t COLOR_DANGER = 0xD2AA;
constexpr uint16_t COLOR_OK = 0x6EEB;
}

Display display_obj;

Display::Display() : tft(nullptr) {}

Display::~Display() {
    if (tft != nullptr) delete tft;
}

void Display::init() {
    if (tft == nullptr) tft = new TFT_eSPI();
    tft->init();
    tft->setRotation(LANDSCAPE_ROTATION);
    tft->fillScreen(COLOR_BG);
    tft->setTextFont(1);
    tft->setTextDatum(TL_DATUM);
    tft->setTextWrap(false, false);
    tft->setTextPadding(0);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, COLOR_BG);
    Serial.printf("[Display] Initialized ST7796 landscape %dx%d\n", tft->width(), tft->height());
}

void Display::fillScreen(uint16_t color) {
    if (tft != nullptr) tft->fillScreen(color);
}

void Display::drawText(int x, int y, const char *text, uint16_t color) {
    if (tft == nullptr) return;
    tft->setTextFont(1);
    tft->setTextColor(color, COLOR_BG);
    tft->setCursor(x, y);
    tft->print(text);
}

void Display::clear() {
    if (tft != nullptr) tft->fillScreen(COLOR_BG);
}

String Display::fitText(const String &text, uint8_t maxChars) const {
    if (text.length() <= maxChars) return text;
    if (maxChars <= 3) return text.substring(0, maxChars);
    return text.substring(0, maxChars - 3) + "...";
}

void Display::drawTab(int x, const char *label, bool active, uint16_t accent) {
    uint16_t fill = active ? accent : COLOR_SURFACE;
    uint16_t text = active ? TFT_BLACK : TFT_LIGHTGREY;
    tft->fillRoundRect(x, TAB_Y, TAB_W, TAB_H, 12, fill);
    tft->drawRoundRect(x, TAB_Y, TAB_W, TAB_H, 12, active ? TFT_WHITE : COLOR_SURFACE_2);
    tft->setTextSize(2);
    tft->setTextColor(text, fill);
    int16_t tx = x + (TAB_W - static_cast<int>(strlen(label)) * 12) / 2;
    tft->setCursor(tx, TAB_Y + 10);
    tft->print(label);
}

void Display::drawHeader(UiTab activeTab, const char *title) {
    tft->fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER);
    tft->drawFastHLine(0, HEADER_H - 1, SCREEN_W, COLOR_SURFACE_2);
    drawTab(TAB_X0, "Einlagern", activeTab == UiTab::STORE, COLOR_ACCENT);
    drawTab(TAB_X0 + (TAB_W + TAB_GAP), "Inventar", activeTab == UiTab::INVENTORY, COLOR_OK);
    drawTab(TAB_X0 + 2 * (TAB_W + TAB_GAP), "Scanner", activeTab == UiTab::SCANNER, COLOR_WARN);
    drawTab(TAB_X0 + 3 * (TAB_W + TAB_GAP), "System", activeTab == UiTab::SYSTEM, COLOR_WARN);

    if (title != nullptr && strlen(title) > 0) {
        tft->setTextSize(1);
        tft->setTextColor(TFT_LIGHTGREY, COLOR_HEADER);
        tft->setCursor(16, HEADER_H + 5);
        tft->print(title);
    }
}

void Display::drawCard(int x, int y, int w, int h, const char *title, uint16_t accent) {
    tft->fillRoundRect(x, y, w, h, 14, COLOR_SURFACE);
    tft->drawRoundRect(x, y, w, h, 14, COLOR_SURFACE_2);
    tft->fillRoundRect(x, y, 6, h, 3, accent);
    tft->setTextFont(1);
    tft->setTextSize(1);
    tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
    tft->setCursor(x + 16, y + 12);
    tft->print(title);
}

void Display::drawStatusPill(int x, int y, const char *label, uint16_t color) {
    int w = static_cast<int>(strlen(label)) * 8 + 18;
    tft->fillRoundRect(x, y, w, 24, 12, color);
    tft->setTextSize(1);
    tft->setTextColor(TFT_BLACK, color);
    tft->setCursor(x + 9, y + 8);
    tft->print(label);
}

void Display::drawButton(int x, int y, int w, int h, const char *label, uint16_t color, uint16_t textColor) {
    tft->fillRoundRect(x, y, w, h, 12, color);
    tft->drawRoundRect(x, y, w, h, 12, TFT_WHITE);
    tft->setTextSize(2);
    tft->setTextColor(textColor, color);
    int16_t tx = x + (w - static_cast<int>(strlen(label)) * 12) / 2;
    if (tx < x + 8) tx = x + 8;
    tft->setCursor(tx, y + 14);
    tft->print(label);
}

void Display::showSplash() {
    if (tft == nullptr) return;
    currentScreen = ScreenKind::MESSAGE;
    tft->startWrite();
    tft->fillScreen(COLOR_BG);
    tft->fillRoundRect(34, 42, SCREEN_W - 68, 186, 24, COLOR_SURFACE);
    tft->drawRoundRect(34, 42, SCREEN_W - 68, 186, 24, COLOR_ACCENT);
    tft->setTextSize(4);
    tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
    tft->setCursor(76, 82);
    tft->print("FoodDock");
    tft->setTextSize(2);
    tft->setTextColor(COLOR_ACCENT, COLOR_SURFACE);
    tft->setCursor(92, 138);
    tft->print("Lebensmittel smart einlagern");
    tft->setTextColor(TFT_LIGHTGREY, COLOR_BG);
    tft->setCursor(158, 258);
    tft->print("System startet...");
    tft->endWrite();
}

void Display::showWiFiStatus(const String &ssid, const String &ip, bool connected) {
    showHome(UiTab::SYSTEM, ssid, ip, connected, "", "", "", "", 0, connected ? "WLAN verbunden" : "Setup-AP aktiv");
}

void Display::showDashboard(const String &ssid,
                            const String &ip,
                            bool wifiConnected,
                            const String &scannerStatus,
                            const String &scannerName,
                            const String &lastScan,
                            const String &lastType,
                            const String &message) {
    showHome(UiTab::STORE, ssid, ip, wifiConnected, scannerStatus, scannerName, lastScan, lastType, 0, message);
}

void Display::showHome(UiTab activeTab,
                       const String &ssid,
                       const String &ip,
                       bool wifiConnected,
                       const String &scannerStatus,
                       const String &scannerName,
                       const String &lastScan,
                       const String &lastType,
                       size_t inventoryCount,
                       const String &message) {
    if (tft == nullptr) return;
    currentScreen = activeTab == UiTab::INVENTORY ? ScreenKind::INVENTORY_LIST : ScreenKind::HOME;
    tft->startWrite();
    tft->fillScreen(COLOR_BG);
    drawHeader(activeTab, activeTab == UiTab::STORE ? "Barcode scannen -> Produktdaten -> MHD -> Menge -> Etikett" : "Status und Verwaltung");

    if (activeTab == UiTab::STORE) {
        drawCard(16, 66, 448, 84, "AKTUELLER SCAN", lastScan.isEmpty() ? COLOR_ACCENT : COLOR_OK);
        tft->setTextSize(2);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(36, 100);
        tft->print(lastScan.isEmpty() ? "Bereit fuer EAN / Label-QR" : fitText(lastScan, 34));
        tft->setTextSize(1);
        tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
        tft->setCursor(36, 128);
        tft->print(lastType.isEmpty() ? "EAN = einlagern, LebNumber-QR = auslagern" : lastType);

        drawCard(16, 166, 214, 72, "INVENTAR", COLOR_OK);
        tft->setTextSize(3);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(38, 195);
        tft->print(static_cast<unsigned>(inventoryCount));
        tft->setTextSize(1);
        tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
        tft->setCursor(92, 205);
        tft->print("Artikel gespeichert");

        drawCard(250, 166, 214, 72, "SCANNER", scannerStatus == "connected" ? COLOR_OK : COLOR_WARN);
        tft->setTextSize(2);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(270, 194);
        tft->print(fitText(scannerStatus, 14));
        tft->setTextSize(1);
        tft->setCursor(270, 218);
        tft->print(fitText(scannerName, 22));
    } else if (activeTab == UiTab::INVENTORY) {
        drawCard(16, 66, 448, 172, "INVENTAR", COLOR_OK);
        tft->setTextSize(3);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(42, 108);
        tft->print(static_cast<unsigned>(inventoryCount));
        tft->setTextSize(2);
        tft->setCursor(42, 156);
        tft->print("Artikel im Lager");
        tft->setTextSize(1);
        tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
        tft->setCursor(42, 198);
        tft->print("Tippe diesen Tab erneut fuer Listenansicht.");
    } else if (activeTab == UiTab::SCANNER) {
        drawCard(16, 66, 448, 172, "BLE-HID SCANNER", scannerStatus == "connected" ? COLOR_OK : COLOR_WARN);
        drawStatusPill(40, 104, scannerStatus.c_str(), scannerStatus == "connected" ? COLOR_OK : COLOR_WARN);
        tft->setTextSize(2);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(40, 148);
        tft->print(fitText(scannerName, 32));
        tft->setTextSize(1);
        tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
        tft->setCursor(40, 196);
        tft->print("Koppeln und Suche laufen im Webinterface.");
    } else {
        drawCard(16, 66, 448, 172, "SYSTEM", wifiConnected ? COLOR_OK : COLOR_WARN);
        drawStatusPill(40, 104, wifiConnected ? "WLAN" : "SETUP-AP", wifiConnected ? COLOR_OK : COLOR_WARN);
        tft->setTextSize(2);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(40, 148);
        tft->print(fitText(wifiConnected ? ssid : String(AP_SSID), 30));
        tft->setCursor(40, 178);
        tft->print(fitText(ip.isEmpty() ? String("192.168.4.1") : ip, 30));
    }

    if (!message.isEmpty()) {
        tft->setTextSize(1);
        tft->setTextColor(TFT_LIGHTGREY, COLOR_BG);
        tft->setCursor(18, 248);
        tft->print(fitText(message, 56));
    }

    drawButton(BTN_REFRESH_X, BTN_Y, BTN_W, BTN_H, "Refresh", COLOR_ACCENT);
    drawButton(BTN_AP_X, BTN_Y, BTN_W, BTN_H, "Setup AP", COLOR_WARN);
    drawButton(BTN_SCANNER_X, BTN_Y, BTN_W, BTN_H, "Scanner", COLOR_OK);
    tft->endWrite();
}

void Display::showFetchingProduct(const String &barcode) {
    if (tft == nullptr) return;
    currentScreen = ScreenKind::MESSAGE;
    tft->startWrite();
    tft->fillScreen(COLOR_BG);
    drawHeader(UiTab::STORE, "Produktdaten werden geladen");
    drawCard(44, 88, 392, 150, "OPEN FOOD FACTS", COLOR_ACCENT);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
    tft->setCursor(74, 132);
    tft->print("Suche Produkt...");
    tft->setCursor(74, 166);
    tft->print(fitText(barcode, 28));
    drawButton(168, BTN_Y, 144, BTN_H, "Abbruch", COLOR_DANGER, TFT_WHITE);
    tft->endWrite();
}

void Display::drawKeypad(const String &value, bool dateMode) {
    tft->fillRoundRect(KEY_X - 12, KEY_Y - 54, 222, 220, 16, COLOR_SURFACE);
    tft->drawRoundRect(KEY_X - 12, KEY_Y - 54, 222, 220, 16, COLOR_SURFACE_2);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
    tft->setCursor(KEY_X, KEY_Y - 36);
    tft->print(value.isEmpty() ? (dateMode ? "JJJJ-MM-TT" : "1") : value);

    const char *labels[12] = {"1","2","3","4","5","6","7","8","9","<","0","OK"};
    for (int i = 0; i < 12; i++) {
        int row = i / 3;
        int col = i % 3;
        uint16_t color = (i == 11) ? COLOR_OK : ((i == 9) ? COLOR_WARN : COLOR_SURFACE_2);
        uint16_t text = (i == 11 || i == 9) ? TFT_BLACK : TFT_WHITE;
        drawButton(KEY_X + col * (KEY_W + KEY_GAP), KEY_Y + row * (KEY_H + KEY_GAP), KEY_W, KEY_H, labels[i], color, text);
    }
}

void Display::showDateEntry(const ProductInfo &product, const String &dateDraft) {
    if (tft == nullptr) return;
    currentScreen = ScreenKind::DATE_ENTRY;
    tft->startWrite();
    tft->fillScreen(COLOR_BG);
    drawHeader(UiTab::STORE, "MHD eingeben");
    drawCard(16, 72, 204, 176, "PRODUKT", COLOR_ACCENT);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
    tft->setCursor(36, 108);
    tft->print(fitText(product.name, 16));
    tft->setTextSize(1);
    tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
    tft->setCursor(36, 142);
    tft->print(fitText(product.brand, 22));
    tft->setCursor(36, 178);
    tft->print("MHD als 8 Ziffern:");
    tft->setCursor(36, 198);
    tft->print("20260514");
    drawKeypad(dateDraft, true);
    drawButton(16, BTN_Y, 132, BTN_H, "Abbruch", COLOR_DANGER, TFT_WHITE);
    tft->endWrite();
}

void Display::showQuantityEntry(const ProductInfo &product, const String &expiryDate, int quantity) {
    if (tft == nullptr) return;
    currentScreen = ScreenKind::QUANTITY_ENTRY;
    tft->startWrite();
    tft->fillScreen(COLOR_BG);
    drawHeader(UiTab::STORE, "Menge bestaetigen");
    drawCard(16, 72, 448, 82, "PRODUKT", COLOR_ACCENT);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
    tft->setCursor(36, 106);
    tft->print(fitText(product.name, 31));
    tft->setTextSize(1);
    tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
    tft->setCursor(36, 132);
    tft->print(String("MHD: ") + expiryDate);

    drawCard(96, 174, 288, 64, "MENGE", COLOR_OK);
    drawButton(112, 190, 56, 38, "-", COLOR_WARN);
    tft->setTextSize(3);
    tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
    tft->setCursor(224, 194);
    tft->print(quantity);
    drawButton(312, 190, 56, 38, "+", COLOR_OK);
    drawButton(16, BTN_Y, 132, BTN_H, "Zurueck", COLOR_DANGER, TFT_WHITE);
    drawButton(284, BTN_Y, 180, BTN_H, "Einlagern", COLOR_OK);
    tft->endWrite();
}

void Display::showResult(const String &title, const String &message, bool success) {
    if (tft == nullptr) return;
    currentScreen = ScreenKind::MESSAGE;
    tft->startWrite();
    tft->fillScreen(COLOR_BG);
    drawHeader(UiTab::STORE, success ? "Abgeschlossen" : "Fehler");
    drawCard(42, 88, 396, 148, title.c_str(), success ? COLOR_OK : COLOR_DANGER);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
    tft->setCursor(68, 142);
    tft->print(fitText(message, 30));
    drawButton(168, BTN_Y, 144, BTN_H, "OK", COLOR_ACCENT);
    tft->endWrite();
}

void Display::showInventoryList(const std::vector<InventoryItem> &items) {
    if (tft == nullptr) return;
    currentScreen = ScreenKind::INVENTORY_LIST;
    tft->startWrite();
    tft->fillScreen(COLOR_BG);
    drawHeader(UiTab::INVENTORY, "Gespeicherte Lebensmittel");
    int y = 62;
    int maxRows = min(static_cast<int>(items.size()), 5);
    for (int i = 0; i < maxRows; i++) {
        const InventoryItem &item = items[items.size() - 1 - i];
        tft->fillRoundRect(16, y, 448, 38, 10, COLOR_SURFACE);
        tft->setTextSize(1);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(28, y + 8);
        tft->print(fitText(item.name, 32));
        tft->setTextColor(TFT_LIGHTGREY, COLOR_SURFACE);
        tft->setCursor(290, y + 8);
        tft->print(String("MHD ") + item.expiryDate);
        tft->setCursor(28, y + 22);
        tft->print(item.labelBarcode);
        y += 42;
    }
    if (items.empty()) {
        drawCard(16, 88, 448, 112, "INVENTAR", COLOR_OK);
        tft->setTextSize(2);
        tft->setTextColor(TFT_WHITE, COLOR_SURFACE);
        tft->setCursor(44, 132);
        tft->print("Noch keine Artikel eingelagert.");
    }
    drawButton(16, BTN_Y, 132, BTN_H, "Zurueck", COLOR_ACCENT);
    tft->endWrite();
}

OnscreenAction Display::hitTest(uint16_t x, uint16_t y) const {
    if (y >= TAB_Y && y <= TAB_Y + TAB_H) {
        if (x >= TAB_X0 && x <= TAB_X0 + TAB_W) return OnscreenAction::TAB_STORE;
        if (x >= TAB_X0 + (TAB_W + TAB_GAP) && x <= TAB_X0 + (TAB_W + TAB_GAP) + TAB_W) return OnscreenAction::TAB_INVENTORY;
        if (x >= TAB_X0 + 2 * (TAB_W + TAB_GAP) && x <= TAB_X0 + 2 * (TAB_W + TAB_GAP) + TAB_W) return OnscreenAction::TAB_SCANNER;
        if (x >= TAB_X0 + 3 * (TAB_W + TAB_GAP) && x <= TAB_X0 + 3 * (TAB_W + TAB_GAP) + TAB_W) return OnscreenAction::TAB_SYSTEM;
    }

    if (currentScreen == ScreenKind::DATE_ENTRY) {
        if (x >= 16 && x <= 148 && y >= BTN_Y && y <= BTN_Y + BTN_H) return OnscreenAction::CANCEL;
        for (int i = 0; i < 12; i++) {
            int row = i / 3;
            int col = i % 3;
            int bx = KEY_X + col * (KEY_W + KEY_GAP);
            int by = KEY_Y + row * (KEY_H + KEY_GAP);
            if (x >= bx && x <= bx + KEY_W && y >= by && y <= by + KEY_H) {
                static const OnscreenAction actions[12] = {
                    OnscreenAction::DATE_DIGIT_1, OnscreenAction::DATE_DIGIT_2, OnscreenAction::DATE_DIGIT_3,
                    OnscreenAction::DATE_DIGIT_4, OnscreenAction::DATE_DIGIT_5, OnscreenAction::DATE_DIGIT_6,
                    OnscreenAction::DATE_DIGIT_7, OnscreenAction::DATE_DIGIT_8, OnscreenAction::DATE_DIGIT_9,
                    OnscreenAction::DATE_BACKSPACE, OnscreenAction::DATE_DIGIT_0, OnscreenAction::DATE_CONFIRM
                };
                return actions[i];
            }
        }
        return OnscreenAction::NONE;
    }

    if (currentScreen == ScreenKind::QUANTITY_ENTRY) {
        if (x >= 16 && x <= 148 && y >= BTN_Y && y <= BTN_Y + BTN_H) return OnscreenAction::CANCEL;
        if (x >= 284 && x <= 464 && y >= BTN_Y && y <= BTN_Y + BTN_H) return OnscreenAction::QTY_CONFIRM;
        if (x >= 112 && x <= 168 && y >= 190 && y <= 228) return OnscreenAction::QTY_MINUS;
        if (x >= 312 && x <= 368 && y >= 190 && y <= 228) return OnscreenAction::QTY_PLUS;
        return OnscreenAction::NONE;
    }

    if (currentScreen == ScreenKind::MESSAGE || currentScreen == ScreenKind::INVENTORY_LIST) {
        if (y >= BTN_Y && y <= BTN_Y + BTN_H) return OnscreenAction::REFRESH;
    }

    if (y < BTN_Y || y > BTN_Y + BTN_H) return OnscreenAction::NONE;
    if (x >= BTN_REFRESH_X && x <= BTN_REFRESH_X + BTN_W) return OnscreenAction::REFRESH;
    if (x >= BTN_AP_X && x <= BTN_AP_X + BTN_W) return OnscreenAction::START_AP;
    if (x >= BTN_SCANNER_X && x <= BTN_SCANNER_X + BTN_W) return OnscreenAction::SCANNER_RECONNECT;
    return OnscreenAction::NONE;
}
