/**
 * display.cpp – Pure TFT_eSPI UI for FoodScanner (Waveshare ESP32-S3-Touch-LCD-3.5)
 * Display: ST7796 SPI, 480×320 landscape  |  Touch: FT6336 I2C
 *
 * Architecture:
 *  - One full-screen TFT_eSprite (480×320, 16-bit) in PSRAM as framebuffer
 *  - All drawing goes to _spr, then _spr.pushSprite(0,0) to commit
 *  - Hit regions: array of HitRegion structs, rebuilt on each screen draw
 *  - Touch: polled in tick(), fires on release, 50 ms min-press debounce
 */

#include "display.h"
#include "touch.h"
#include "models/InventoryItem.h"
#include "models/ProductInfo.h"
#include "config.h"

#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

// ─────────────────── layout constants ────────────────────
static constexpr int SCR_W  = DISPLAY_LANDSCAPE_WIDTH;   // 480
static constexpr int SCR_H  = DISPLAY_LANDSCAPE_HEIGHT;  // 320
static constexpr int HDR_H  = 44;
static constexpr int TAB_H  = 52;
static constexpr int CNT_Y  = HDR_H;
static constexpr int CNT_H  = SCR_H - HDR_H - TAB_H;    // 224

// ─────────────────── color palette (RGB565) ──────────────
// Helper: pack RGB888 to RGB565
#define RGB(r,g,b) ( (uint16_t)(((r)&0xF8)<<8) | (uint16_t)(((g)&0xFC)<<3) | (uint16_t)((b)>>3) )

static constexpr uint16_t C_BG       = RGB(0x0D,0x11,0x17);
static constexpr uint16_t C_SURFACE  = RGB(0x16,0x1B,0x22);
static constexpr uint16_t C_SURFACE2 = RGB(0x21,0x26,0x2D);
static constexpr uint16_t C_BORDER   = RGB(0x30,0x36,0x3D);
static constexpr uint16_t C_TEXT     = RGB(0xE6,0xED,0xF3);
static constexpr uint16_t C_SUBTEXT  = RGB(0x8B,0x94,0x9E);
static constexpr uint16_t C_ACCENT   = RGB(0x58,0xA6,0xFF);
static constexpr uint16_t C_GREEN    = RGB(0x3F,0xB9,0x50);
static constexpr uint16_t C_YELLOW   = RGB(0xD2,0x99,0x22);
static constexpr uint16_t C_RED      = RGB(0xF8,0x51,0x49);

// ─────────────────── hit region table ────────────────────
static constexpr int MAX_REGIONS = 32;

struct HitRegion {
    int16_t x, y, w, h;
    OnscreenAction action;
};

static HitRegion  _regions[MAX_REGIONS];
static int        _region_count = 0;

static void clear_regions() { _region_count = 0; }

static void add_region(int16_t x, int16_t y, int16_t w, int16_t h, OnscreenAction a) {
    if (_region_count >= MAX_REGIONS) return;
    _regions[_region_count++] = {x, y, w, h, a};
}

// ─────────────────── touch debounce state ────────────────
static bool     _touch_was_pressed = false;
static uint32_t _touch_press_ms    = 0;
static int16_t  _touch_press_x     = 0;
static int16_t  _touch_press_y     = 0;

// ─────────────────── action queue (single-slot) ──────────
static volatile OnscreenAction _pending_action = OnscreenAction::NONE;

// ─────────────────── TFT + sprite ────────────────────────
static TFT_eSPI    _tft;
static TFT_eSprite _spr(&_tft);

// ─────────────────── current screen state (for overlays) ─
// We remember the "home" state so overlay screens can be drawn over it
struct HomeState {
    UiTab  tab;
    bool   wifiConnected;
    String ssid, ip;
    String scannerStatus, scannerName;
    String lastScan, lastType;
    size_t inventoryCount;
    String message;
    bool   valid = false;
};
static HomeState _homeState;

// ═════════════════════════════════════════════════════════
//   Low-level drawing helpers
// ═════════════════════════════════════════════════════════

// Truncate a String to maxChars, appending "…" if cut
static String trunc(const String &s, int maxChars) {
    if ((int)s.length() <= maxChars) return s;
    return s.substring(0, maxChars - 1) + "~";
}

// Draw a filled rounded-rect button and register its hit region
static void draw_button(int x, int y, int w, int h,
                        const char* label, uint16_t bg, uint16_t fg,
                        uint8_t font, OnscreenAction action) {
    _spr.fillRoundRect(x, y, w, h, 8, bg);
    _spr.drawRoundRect(x, y, w, h, 8, C_BORDER);
    _spr.setTextColor(fg, bg);
    _spr.setTextFont(font);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(label, x + w / 2, y + h / 2);
    if (action != OnscreenAction::NONE)
        add_region(x, y, w, h, action);
}

// Draw a card (rounded rect with border)
static void draw_card(int x, int y, int w, int h,
                      uint16_t bg = C_SURFACE, uint16_t border = C_BORDER) {
    _spr.fillRoundRect(x, y, w, h, 10, bg);
    _spr.drawRoundRect(x, y, w, h, 10, border);
}

// Draw text with a specific datum at (x,y)
static void draw_text(int x, int y, const char* text,
                      uint16_t color, uint8_t font, uint8_t datum = TL_DATUM) {
    _spr.setTextColor(color, C_BG);
    _spr.setTextFont(font);
    _spr.setTextDatum(datum);
    _spr.drawString(text, x, y);
}

// Draw text inside a card (bg-transparent)
static void draw_text_on(int x, int y, const char* text,
                         uint16_t color, uint8_t font,
                         uint16_t bg, uint8_t datum = TL_DATUM) {
    _spr.setTextColor(color, bg);
    _spr.setTextFont(font);
    _spr.setTextDatum(datum);
    _spr.drawString(text, x, y);
}

// Draw a small pill badge
static void draw_pill(int x, int y, const char* label, uint16_t bg) {
    _spr.setTextFont(2);
    int tw = _spr.textWidth(label) + 16;
    _spr.fillRoundRect(x, y, tw, 22, 11, bg);
    _spr.setTextColor(C_TEXT, bg);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(label, x + tw / 2, y + 11);
}

// ═════════════════════════════════════════════════════════
//   Header bar (shared by home screens)
// ═════════════════════════════════════════════════════════

static void draw_header(bool wifiConnected) {
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);

    // Title
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);   // ~26px
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("FoodScanner", 12, HDR_H / 2);

    // WiFi dot
    uint16_t wc = wifiConnected ? C_GREEN : C_RED;
    _spr.fillCircle(SCR_W - 24, HDR_H / 2, 7, wc);
    _spr.drawCircle(SCR_W - 24, HDR_H / 2, 7, C_BORDER);
    // Small "W" label inside
    _spr.setTextColor(C_SURFACE, wc);
    _spr.setTextFont(1);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("W", SCR_W - 24, HDR_H / 2);
}

// ═════════════════════════════════════════════════════════
//   Tab bar
// ═════════════════════════════════════════════════════════

static const char* _tab_labels[4] = { "HOME", "INVENTAR", "SCANNER", "SYSTEM" };
static const OnscreenAction _tab_actions[4] = {
    OnscreenAction::TAB_STORE, OnscreenAction::TAB_INVENTORY,
    OnscreenAction::TAB_SCANNER, OnscreenAction::TAB_SYSTEM
};

static void draw_tabbar(UiTab activeTab) {
    int tab_y = SCR_H - TAB_H;
    _spr.fillRect(0, tab_y, SCR_W, TAB_H, C_SURFACE);
    _spr.drawFastHLine(0, tab_y, SCR_W, C_BORDER);

    int btn_w = SCR_W / 4;  // 120 px each
    for (int i = 0; i < 4; i++) {
        bool active = (i == static_cast<int>(activeTab));
        uint16_t bg = active ? C_ACCENT  : C_SURFACE;
        uint16_t fg = active ? C_BG      : C_SUBTEXT;

        int tx = i * btn_w;
        _spr.fillRect(tx, tab_y + 1, btn_w, TAB_H - 1, bg);

        _spr.setTextColor(fg, bg);
        _spr.setTextFont(2);   // small
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(_tab_labels[i], tx + btn_w / 2, tab_y + TAB_H / 2);

        add_region(tx, tab_y, btn_w, TAB_H, _tab_actions[i]);

        // Divider between tabs
        if (i > 0)
            _spr.drawFastVLine(tx, tab_y, TAB_H, C_BORDER);
    }
}

// ═════════════════════════════════════════════════════════
//   Content panel: STORE (Dashboard)
// ═════════════════════════════════════════════════════════

static void draw_panel_store(const HomeState &s) {
    int py = CNT_Y;  // 44

    // ── 4 stat cards (y=48, h=64) ────────────────────────
    // Card width fits 4 across with 4px gaps: (480 - 5*4)/4 = 115
    static const char*   stat_labels[4] = { "Produkte", "Ablaufend", "Kritisch", "Einkauf" };
    static const uint16_t stat_colors[4] = { C_ACCENT, C_YELLOW, C_RED, C_GREEN };
    int card_w = 115, card_h = 64;
    for (int i = 0; i < 4; i++) {
        int cx = 4 + i * (card_w + 4);
        int cy = py + 4;
        draw_card(cx, cy, card_w, card_h, C_SURFACE, stat_colors[i]);

        // Left accent bar
        _spr.fillRect(cx + 1, cy + 1, 4, card_h - 2, stat_colors[i]);

        // Count
        const char* count = (i == 0) ? String(s.inventoryCount).c_str() : "0";
        _spr.setTextColor(stat_colors[i], C_SURFACE);
        _spr.setTextFont(4);
        _spr.setTextDatum(TL_DATUM);
        _spr.drawString(count, cx + 10, cy + 8);

        // Label
        _spr.setTextColor(C_SUBTEXT, C_SURFACE);
        _spr.setTextFont(2);
        _spr.setTextDatum(TL_DATUM);
        _spr.drawString(stat_labels[i], cx + 10, cy + 38);
    }

    // ── Status pills row (y=116) ──────────────────────────
    int pill_y = py + 72;
    uint16_t wifi_bg = s.wifiConnected ? C_GREEN : C_RED;
    bool ble_ok = (s.scannerStatus == "connected");
    uint16_t ble_bg = ble_ok ? C_GREEN : C_SURFACE2;

    _spr.setTextFont(2);
    String wifi_pill = s.wifiConnected ? "WLAN OK" : "WLAN FEHLT";
    draw_pill(4, pill_y, wifi_pill.c_str(), wifi_bg);

    int pill_x2 = 4 + (int)_spr.textWidth(wifi_pill.c_str()) + 20;
    String ble_pill = ble_ok ? "BLE OK" : "BLE ---";
    draw_pill(pill_x2, pill_y, ble_pill.c_str(), ble_bg);

    // ── Last scan card (y=142, h=72) ─────────────────────
    int scan_y = py + 98;
    draw_card(4, scan_y, 234, 72, C_SURFACE, C_ACCENT);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("LETZTER SCAN", 12, scan_y + 6);

    String scan_code = s.lastScan.isEmpty() ? "Bereit zum Scannen" : trunc(s.lastScan, 26);
    String scan_type = s.lastScan.isEmpty() ? "EAN scannen oder Label-QR" : trunc(s.lastType, 30);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.drawString(scan_code.c_str(), 12, scan_y + 26);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.drawString(scan_type.c_str(), 12, scan_y + 46);

    // ── Bald ablaufend card (y=142, x=242, h=72) ─────────
    draw_card(242, scan_y, 234, 72, C_SURFACE, C_YELLOW);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("BALD ABLAUFEND", 250, scan_y + 6);
    _spr.setTextColor(C_YELLOW, C_SURFACE);
    _spr.drawString("Keine Eintraege ablaufend", 250, scan_y + 26);

    // ── Activity line (y=222, h=40) ───────────────────────
    int act_y = py + 178;
    draw_card(4, act_y, SCR_W - 8, 40, C_SURFACE, C_ACCENT);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("LETZTE AKTIVITAET", 12, act_y + 4);
    String msg = s.message.isEmpty() ? "System bereit" : trunc(s.message, 60);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.drawString(msg.c_str(), 12, act_y + 22);
}

// ═════════════════════════════════════════════════════════
//   Content panel: INVENTORY LIST
// ═════════════════════════════════════════════════════════

static void draw_panel_inventory_empty(const HomeState &s) {
    int py = CNT_Y;
    // Column headers
    _spr.fillRect(0, py, SCR_W, 28, C_SURFACE2);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE2);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("Produkt",  8,  py + 7);
    _spr.drawString("MHD",      300, py + 7);
    _spr.drawString("Menge",    420, py + 7);
    _spr.drawFastHLine(0, py + 28, SCR_W, C_BORDER);

    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("Keine Eintraege", SCR_W / 2, py + 80);

    String count_str = String(s.inventoryCount) + " Eintraege";
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString(count_str.c_str(), 8, CNT_Y + CNT_H - 18);
}

// ═════════════════════════════════════════════════════════
//   Content panel: SCANNER
// ═════════════════════════════════════════════════════════

static void draw_panel_scanner(const HomeState &s) {
    int py = CNT_Y;
    bool ble_ok = (s.scannerStatus == "connected");

    draw_card(4, py + 8, SCR_W - 8, 110, C_SURFACE, C_YELLOW);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("BLE-HID BARCODE SCANNER", 12, py + 14);

    uint16_t sc = ble_ok ? C_GREEN : C_YELLOW;
    _spr.setTextColor(sc, C_SURFACE);
    _spr.setTextFont(4);
    _spr.drawString(ble_ok ? "Verbunden" : "Getrennt", 12, py + 36);

    String name_str = s.scannerName.isEmpty() ? "Koppeln ueber Web-UI" : trunc(s.scannerName, 32);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.drawString(name_str.c_str(), 12, py + 70);

    draw_button(SCR_W - 220, py + 80, 210, 34,
                "Verbinden / Trennen",
                C_ACCENT, C_BG, 2, OnscreenAction::SCANNER_RECONNECT);
}

// ═════════════════════════════════════════════════════════
//   Content panel: SYSTEM
// ═════════════════════════════════════════════════════════

static void draw_panel_system(const HomeState &s) {
    int py = CNT_Y;

    // WLAN card
    draw_card(4, py + 8, 230, 120, C_SURFACE, C_GREEN);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("NETZWERK", 12, py + 14);

    uint16_t wc = s.wifiConnected ? C_GREEN : C_RED;
    _spr.setTextColor(wc, C_SURFACE);
    _spr.setTextFont(4);
    _spr.drawString(s.wifiConnected ? "Verbunden" : "Nicht verbunden", 12, py + 34);

    String ssid_str = s.wifiConnected ? trunc(s.ssid, 22) : "Setup-AP: " + String(AP_SSID);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.drawString(ssid_str.c_str(), 12, py + 66);

    String ip_str = s.ip.isEmpty() ? "192.168.4.1" : s.ip;
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.drawString(ip_str.c_str(), 12, py + 84);

    draw_button(12, py + 100, 210, 30,
                "Setup-AP starten",
                C_YELLOW, C_BG, 2, OnscreenAction::START_AP);

    // Device info card
    draw_card(242, py + 8, 234, 120, C_SURFACE, C_ACCENT);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("GERAET", 250, py + 14);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.drawString("FoodScanner ESP32-S3", 250, py + 34);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.drawString("ST7796 | FT6336 | 480x320", 250, py + 54);

    // Refresh button
    draw_button(4, SCR_H - TAB_H - 44, 150, 36,
                "Refresh",
                C_ACCENT, C_TEXT, 2, OnscreenAction::REFRESH);
}

// ═════════════════════════════════════════════════════════
//   Commit sprite to display
// ═════════════════════════════════════════════════════════

static void commit() {
    _spr.pushSprite(0, 0);
}

// ═════════════════════════════════════════════════════════
//   Full home screen renderer
// ═════════════════════════════════════════════════════════

static void render_home(const HomeState &s) {
    _spr.fillSprite(C_BG);
    clear_regions();

    draw_header(s.wifiConnected);
    draw_tabbar(s.tab);

    // Fill content area background
    _spr.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG);

    switch (s.tab) {
        case UiTab::STORE:
            draw_panel_store(s);
            break;
        case UiTab::INVENTORY:
            draw_panel_inventory_empty(s);
            break;
        case UiTab::SCANNER:
            draw_panel_scanner(s);
            break;
        case UiTab::SYSTEM:
            draw_panel_system(s);
            break;
    }

    commit();
}

// ═════════════════════════════════════════════════════════
//   Display class implementation
// ═════════════════════════════════════════════════════════

Display display_obj;

Display::Display() {}
Display::~Display() {}

void Display::init() {
    if (_initialized) return;

    _tft.init();
    _tft.setRotation(1);   // landscape 480×320
    _tft.fillScreen(TFT_BLACK);

    // Allocate full-screen sprite in PSRAM
    _spr.setColorDepth(16);
    bool ok = _spr.createSprite(SCR_W, SCR_H);
    if (!ok) {
        Serial.println("[Display] PSRAM sprite failed – trying internal RAM");
        // TFT_eSPI will fall back to internal heap automatically
        _spr.createSprite(SCR_W, SCR_H);
    }
    _spr.setSwapBytes(false);   // TFT_eSPI handles byte order

    _initialized = true;
    Serial.printf("[Display] TFT_eSPI sprite ready (%d x %d)\n", SCR_W, SCR_H);
}

void Display::tick() {
    if (!_initialized) return;

    TouchPoint tp = touch_obj.read();

    if (tp.pressed) {
        if (!_touch_was_pressed) {
            // Rising edge – record press location and time
            _touch_was_pressed = true;
            _touch_press_ms    = millis();
            _touch_press_x     = tp.x;
            _touch_press_y     = tp.y;
        }
    } else {
        if (_touch_was_pressed) {
            // Falling edge – check debounce and hit
            uint32_t held = millis() - _touch_press_ms;
            if (held >= 50) {
                // Fire action for the press location
                for (int i = 0; i < _region_count; i++) {
                    const HitRegion &r = _regions[i];
                    if (_touch_press_x >= r.x && _touch_press_x < r.x + r.w &&
                        _touch_press_y >= r.y && _touch_press_y < r.y + r.h) {
                        _pending_action = r.action;
                        break;
                    }
                }
            }
            _touch_was_pressed = false;
        }
    }
}

OnscreenAction Display::hitTest(uint16_t /*x*/, uint16_t /*y*/) const {
    OnscreenAction a = _pending_action;
    _pending_action  = OnscreenAction::NONE;
    return a;
}

void Display::fillScreen(uint16_t color) {
    _tft.fillScreen(color);
}

void Display::drawText(int x, int y, const char* text, uint16_t color) {
    _tft.setTextColor(color);
    _tft.drawString(text, x, y);
}

void Display::clear() {
    if (_initialized) {
        _spr.fillSprite(C_BG);
        commit();
    }
}

// ─────────────────────────────────────────────────────────
//   Splash screen
// ─────────────────────────────────────────────────────────

void Display::showSplash() {
    if (!_initialized) return;
    clear_regions();

    _spr.fillSprite(C_BG);

    // Logo box centered
    int bw = 360, bh = 160;
    int bx = (SCR_W - bw) / 2;
    int by = (SCR_H - bh) / 2 - 10;
    _spr.fillRoundRect(bx, by, bw, bh, 16, C_SURFACE);
    _spr.drawRoundRect(bx, by, bw, bh, 16, C_ACCENT);

    // App name – font 6 (48px)
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(6);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("FoodScanner", SCR_W / 2, by + 20);

    // Subtitle
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Lebensmittel smart verwalten", SCR_W / 2, by + 86);

    // Boot message at bottom
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(BC_DATUM);
    _spr.drawString("System startet...", SCR_W / 2, SCR_H - 16);

    commit();
}

// ─────────────────────────────────────────────────────────
//   WiFi status (delegates to showHome/SYSTEM)
// ─────────────────────────────────────────────────────────

void Display::showWiFiStatus(const String &ssid, const String &ip, bool connected) {
    showHome(UiTab::SYSTEM, ssid, ip, connected, "", "", "", "", 0,
             connected ? "WLAN verbunden" : "Setup-AP aktiv");
}

// ─────────────────────────────────────────────────────────
//   Dashboard (delegates to showHome/STORE)
// ─────────────────────────────────────────────────────────

void Display::showDashboard(const String &ssid, const String &ip, bool wifiConnected,
                             const String &scannerStatus, const String &scannerName,
                             const String &lastScan, const String &lastType,
                             const String &message) {
    showHome(UiTab::STORE, ssid, ip, wifiConnected, scannerStatus, scannerName,
             lastScan, lastType, _homeState.inventoryCount, message);
}

// ─────────────────────────────────────────────────────────
//   Home screen (main entry)
// ─────────────────────────────────────────────────────────

void Display::showHome(UiTab activeTab,
                       const String &ssid, const String &ip, bool wifiConnected,
                       const String &scannerStatus, const String &scannerName,
                       const String &lastScan, const String &lastType,
                       size_t inventoryCount, const String &message) {
    if (!_initialized) return;

    _homeState.tab            = activeTab;
    _homeState.wifiConnected  = wifiConnected;
    _homeState.ssid           = ssid;
    _homeState.ip             = ip;
    _homeState.scannerStatus  = scannerStatus;
    _homeState.scannerName    = scannerName;
    _homeState.lastScan       = lastScan;
    _homeState.lastType       = lastType;
    _homeState.inventoryCount = inventoryCount;
    _homeState.message        = message;
    _homeState.valid          = true;

    render_home(_homeState);
}

// ─────────────────────────────────────────────────────────
//   Fetching product overlay
// ─────────────────────────────────────────────────────────

void Display::showFetchingProduct(const String &barcode) {
    if (!_initialized) return;

    // Draw home as background if available
    if (_homeState.valid) render_home(_homeState);
    else { _spr.fillSprite(C_BG); }

    // Semi-transparent overlay (darken by overpainting with alpha approximation)
    // We simulate by drawing a semi-dark rect (no real alpha in 565)
    // Draw dialog
    int dw = 380, dh = 130;
    int dx = (SCR_W - dw) / 2;
    int dy = (SCR_H - dh) / 2;

    _spr.fillRoundRect(dx, dy, dw, dh, 12, C_SURFACE);
    _spr.drawRoundRect(dx, dy, dw, dh, 12, C_ACCENT);

    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Open Food Facts - Suche...", dx + dw / 2, dy + 14);

    // Barcode
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString(trunc(barcode, 24).c_str(), dx + dw / 2, dy + 44);

    // Spinning-dot indicator (simple static dots)
    for (int i = 0; i < 3; i++)
        _spr.fillCircle(dx + dw / 2 - 16 + i * 16, dy + 92, 5, C_ACCENT);

    // Cancel button
    clear_regions();
    draw_button(dx + dw - 140, dy + dh - 44, 124, 34,
                "Abbrechen", C_RED, C_TEXT, 2, OnscreenAction::CANCEL);

    commit();
}

// ─────────────────────────────────────────────────────────
//   Date entry – full-screen with large numpad
// ─────────────────────────────────────────────────────────

void Display::showDateEntry(const ProductInfo &product, const String &dateDraft) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // ── Header (44px) ──────────────────────────────────────
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);

    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(trunc(product.name, 28).c_str(), 8, HDR_H / 2);

    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextDatum(MR_DATUM);
    _spr.drawString("MHD eingeben", SCR_W - 8, HDR_H / 2);

    // ── Date display area (y=44, h=72) ────────────────────
    _spr.fillRect(0, HDR_H, SCR_W, 72, C_SURFACE2);
    _spr.drawFastHLine(0, HDR_H + 72 - 1, SCR_W, C_BORDER);

    // Format draft: "DDMMJJ" → "DD.MM.JJ" with underscore placeholders
    String d = dateDraft;
    while ((int)d.length() < 6) d += '_';
    String fmt = d.substring(0, 2) + "." + d.substring(2, 4) + "." + d.substring(4, 6);

    _spr.setTextColor(C_ACCENT, C_SURFACE2);
    _spr.setTextFont(6);   // 48px
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(fmt.c_str(), SCR_W / 2, HDR_H + 36);

    // ── Numpad (y=116, remaining = 320-116 = 204px) ───────
    // 3 columns × 4 rows, full-width 480px
    // Button size: 160 × 51px (3*160=480, 4*51=204)
    static const char* keys[12] = {
        "1","2","3","4","5","6","7","8","9","<","0","OK"
    };
    static const OnscreenAction key_actions[12] = {
        OnscreenAction::DATE_DIGIT_1, OnscreenAction::DATE_DIGIT_2, OnscreenAction::DATE_DIGIT_3,
        OnscreenAction::DATE_DIGIT_4, OnscreenAction::DATE_DIGIT_5, OnscreenAction::DATE_DIGIT_6,
        OnscreenAction::DATE_DIGIT_7, OnscreenAction::DATE_DIGIT_8, OnscreenAction::DATE_DIGIT_9,
        OnscreenAction::DATE_BACKSPACE, OnscreenAction::DATE_DIGIT_0, OnscreenAction::DATE_CONFIRM
    };

    int numpad_y = HDR_H + 72;    // 116
    int btn_w = 160;               // exactly 3 × 160 = 480
    int btn_h = 51;                // exactly 4 × 51 = 204

    for (int i = 0; i < 12; i++) {
        int row = i / 3;
        int col = i % 3;
        int bx  = col * btn_w;
        int by  = numpad_y + row * btn_h;

        uint16_t bg = (i == 11) ? C_GREEN
                    : (i == 9)  ? C_YELLOW
                    : C_SURFACE2;
        uint16_t fg = (i == 11 || i == 9) ? C_BG : C_TEXT;

        // Fill button background
        _spr.fillRect(bx, by, btn_w, btn_h, bg);

        // Border / separators
        _spr.drawFastHLine(bx, by, btn_w, C_BORDER);
        _spr.drawFastVLine(bx, by, btn_h, C_BORDER);

        // Label – use font 6 (48px) for digits, font 4 for special keys
        if (i == 9) {
            // Backspace
            _spr.setTextColor(fg, bg);
            _spr.setTextFont(4);
            _spr.setTextDatum(MC_DATUM);
            _spr.drawString("<--", bx + btn_w / 2, by + btn_h / 2);
        } else if (i == 11) {
            // OK
            _spr.setTextColor(fg, bg);
            _spr.setTextFont(6);
            _spr.setTextDatum(MC_DATUM);
            _spr.drawString("OK", bx + btn_w / 2, by + btn_h / 2);
        } else {
            _spr.setTextColor(fg, bg);
            _spr.setTextFont(6);
            _spr.setTextDatum(MC_DATUM);
            _spr.drawString(keys[i], bx + btn_w / 2, by + btn_h / 2);
        }

        add_region(bx, by, btn_w, btn_h, key_actions[i]);
    }

    // Bottom border of last row
    _spr.drawFastHLine(0, numpad_y + 4 * btn_h, SCR_W, C_BORDER);

    commit();
}

// ─────────────────────────────────────────────────────────
//   Quantity entry – full-screen
// ─────────────────────────────────────────────────────────

void Display::showQuantityEntry(const ProductInfo &product,
                                 const String &expiryDate, int quantity) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // ── Header ──────────────────────────────────────────────
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);

    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(trunc(product.name, 24).c_str(), 8, HDR_H / 2);

    String mhd_str = "MHD " + expiryDate;
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextDatum(MR_DATUM);
    _spr.drawString(mhd_str.c_str(), SCR_W - 8, HDR_H / 2);

    // ── Title ────────────────────────────────────────────────
    _spr.setTextColor(C_ACCENT, C_BG);
    _spr.setTextFont(4);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Menge bestaetigen", SCR_W / 2, HDR_H + 12);

    // ── Large quantity display ────────────────────────────────
    int qty_center_y = HDR_H + 100;
    _spr.setTextColor(C_TEXT, C_BG);
    _spr.setTextFont(6);   // 48px
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(String(quantity).c_str(), SCR_W / 2, qty_center_y);

    // ── Minus / Plus buttons ──────────────────────────────────
    int btn_y = qty_center_y - 35;
    draw_button(SCR_W / 2 - 140, btn_y, 90, 70,
                "-", C_YELLOW, C_BG, 6, OnscreenAction::QTY_MINUS);
    draw_button(SCR_W / 2 + 50,  btn_y, 90, 70,
                "+", C_GREEN,  C_BG, 6, OnscreenAction::QTY_PLUS);

    // ── Bottom buttons ───────────────────────────────────────
    int bot_y = SCR_H - 60;
    draw_button(4,             bot_y, 120, 44,
                "Zurueck", C_RED, C_TEXT, 2, OnscreenAction::CANCEL);
    draw_button(SCR_W - 174,  bot_y, 170, 44,
                "Einlagern ->", C_GREEN, C_BG, 2, OnscreenAction::QTY_CONFIRM);

    commit();
}

// ─────────────────────────────────────────────────────────
//   Result screen
// ─────────────────────────────────────────────────────────

void Display::showResult(const String &title, const String &message, bool success) {
    if (!_initialized) return;

    // Background
    if (_homeState.valid) render_home(_homeState);
    else _spr.fillSprite(C_BG);

    // Dialog box
    int dw = 380, dh = 160;
    int dx = (SCR_W - dw) / 2;
    int dy = (SCR_H - dh) / 2;

    _spr.fillRoundRect(dx, dy, dw, dh, 12, C_SURFACE);
    _spr.drawRoundRect(dx, dy, dw, dh, 12, C_BORDER);

    uint16_t tc = success ? C_GREEN : C_RED;
    _spr.setTextColor(tc, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString(trunc(title, 28).c_str(), dx + dw / 2, dy + 14);

    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    // Simple word wrap: split at 44-char mark
    String msg = message;
    if ((int)msg.length() <= 44) {
        _spr.drawString(msg.c_str(), dx + dw / 2, dy + 58);
    } else {
        _spr.drawString(msg.substring(0, 44).c_str(), dx + dw / 2, dy + 54);
        _spr.drawString(msg.substring(44).c_str(),    dx + dw / 2, dy + 72);
    }

    clear_regions();
    draw_button(dx + dw / 2 - 60, dy + dh - 50, 120, 36,
                "OK", C_ACCENT, C_TEXT, 2, OnscreenAction::REFRESH);

    commit();
}

// ─────────────────────────────────────────────────────────
//   Inventory list screen
// ─────────────────────────────────────────────────────────

void Display::showInventoryList(const std::vector<InventoryItem> &items) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // ── Header ──────────────────────────────────────────────
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("Inventar", 12, HDR_H / 2);

    String count_str = String(items.size()) + " Eintraege";
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(MR_DATUM);
    _spr.drawString(count_str.c_str(), SCR_W - 8, HDR_H / 2);

    // ── Column headers ───────────────────────────────────────
    _spr.fillRect(0, HDR_H, SCR_W, 28, C_SURFACE2);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE2);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("Produkt", 8,   HDR_H + 7);
    _spr.drawString("MHD",    310,  HDR_H + 7);
    _spr.drawString("Menge",  430,  HDR_H + 7);
    _spr.drawFastHLine(0, HDR_H + 28, SCR_W, C_BORDER);

    // ── List rows – max 8 visible at 28px each ────────────────
    static constexpr int ROW_H    = 28;
    static constexpr int MAX_ROWS = 8;
    int list_y = HDR_H + 28;

    int shown = 0;
    int start = static_cast<int>(items.size()) - 1;
    for (int i = start; i >= 0 && shown < MAX_ROWS; i--, shown++) {
        const InventoryItem &item = items[i];
        int ry = list_y + shown * ROW_H;
        uint16_t row_bg = (shown % 2 == 0) ? C_SURFACE : C_SURFACE2;
        _spr.fillRect(0, ry, SCR_W, ROW_H, row_bg);

        _spr.setTextColor(C_TEXT,    row_bg);
        _spr.setTextFont(2);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(trunc(item.name, 22).c_str(), 8, ry + ROW_H / 2);

        _spr.setTextColor(C_TEXT, row_bg);
        _spr.drawString(trunc(item.expiryDate, 10).c_str(), 310, ry + ROW_H / 2);

        _spr.setTextColor(C_ACCENT, row_bg);
        _spr.drawString(String(item.quantity).c_str(), 430, ry + ROW_H / 2);

        _spr.drawFastHLine(0, ry + ROW_H - 1, SCR_W, C_BORDER);
    }

    if (items.empty()) {
        _spr.setTextColor(C_SUBTEXT, C_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString("Keine Eintraege", SCR_W / 2, list_y + 60);
    }

    // Draw tab bar at bottom so user can navigate away
    draw_tabbar(UiTab::INVENTORY);

    // Update cached home state for overlay backgrounds
    _homeState.inventoryCount = items.size();

    commit();
}
