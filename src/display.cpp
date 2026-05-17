/**
 * display.cpp – Pure TFT_eSPI UI for FoodScanner (Waveshare ESP32-S3-Touch-LCD-3.5)
 * Display: ST7796 SPI, 480×320 landscape  |  Touch: FT6336 I2C
 *
 * Architecture:
 *  - One full-screen TFT_eSprite (480×320, 16-bit) in PSRAM as framebuffer
 *  - All drawing goes to _spr, then _spr.pushSprite(0,0) to commit
 *  - Hit regions: array of HitRegion structs, rebuilt on each screen draw
 *  - Touch: polled in tick(), swipe detected on release (>80px horizontal)
 *  - Keyboard: KB_CHAR action with char in _pending_kb_char
 */

#include "display.h"
#include "touch.h"
#include "models/InventoryItem.h"
#include "models/ProductInfo.h"
#include "config.h"

#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <qrcode.h>

// ─────────────────── layout constants ────────────────────
static constexpr int SCR_W  = DISPLAY_LANDSCAPE_WIDTH;   // 480
static constexpr int SCR_H  = DISPLAY_LANDSCAPE_HEIGHT;  // 320
static constexpr int HDR_H  = 44;
static constexpr int TAB_H  = 0;   // removed — navigation via swipe only
static constexpr int CNT_Y  = HDR_H;
static constexpr int CNT_H  = SCR_H - HDR_H;              // 276

// ─────────────────── tab bar constants ───────────────────
static constexpr int TAB_COUNT = 6;  // STORE · INVENTAR · SCANNER · SYSTEM · PRODUKTE · EINGABE

// ─────────────────── color palette (RGB565) ──────────────
#define RGB(r,g,b) ( (uint16_t)(((r)&0xF8)<<8) | (uint16_t)(((g)&0xFC)<<3) | (uint16_t)((b)>>3) )

static constexpr uint16_t C_BG       = RGB(0x08,0x0C,0x10);
static constexpr uint16_t C_SURFACE  = RGB(0x12,0x17,0x1E);
static constexpr uint16_t C_SURFACE2 = RGB(0x1C,0x22,0x2A);
static constexpr uint16_t C_BORDER   = RGB(0x28,0x2E,0x38);
static constexpr uint16_t C_TEXT     = RGB(0xEC,0xF0,0xF4);
static constexpr uint16_t C_SUBTEXT  = RGB(0x7A,0x84,0x90);
static constexpr uint16_t C_ACCENT   = RGB(0x4C,0x9E,0xFF);
static constexpr uint16_t C_GREEN    = RGB(0x2E,0xB0,0x48);
static constexpr uint16_t C_YELLOW   = RGB(0xCC,0x92,0x18);
static constexpr uint16_t C_RED      = RGB(0xF0,0x46,0x40);

// ─────────────────── hit region table ────────────────────
static constexpr int MAX_REGIONS = 48;

struct HitRegion {
    int16_t x, y, w, h;
    OnscreenAction action;
    char     extra_char = 0;  // non-zero for KB_CHAR regions
};

static HitRegion  _regions[MAX_REGIONS];
static int        _region_count = 0;
static portMUX_TYPE _regions_mux = portMUX_INITIALIZER_UNLOCKED;

static void clear_regions() {
    portENTER_CRITICAL(&_regions_mux);
    _region_count = 0;
    portEXIT_CRITICAL(&_regions_mux);
}

static void add_region(int16_t x, int16_t y, int16_t w, int16_t h, OnscreenAction a, char extra = 0) {
    portENTER_CRITICAL(&_regions_mux);
    if (_region_count < MAX_REGIONS)
        _regions[_region_count++] = {x, y, w, h, a, extra};
    portEXIT_CRITICAL(&_regions_mux);
}

// ─────────────────── active location (shared across all screens) ─────────
static String   _s_active_location;
static uint16_t _s_location_color = 0;  // 0 = use C_ACCENT default

static uint16_t hexToRgb565(const String &hex) {
    String h = hex.startsWith("#") ? hex.substring(1) : hex;
    if (h.length() != 6) return 0;
    uint8_t r = (uint8_t)strtol(h.substring(0, 2).c_str(), nullptr, 16);
    uint8_t g = (uint8_t)strtol(h.substring(2, 4).c_str(), nullptr, 16);
    uint8_t b = (uint8_t)strtol(h.substring(4, 6).c_str(), nullptr, 16);
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

// ─────────────────── touch debounce state ────────────────
static bool     _touch_was_pressed = false;
static uint32_t _touch_press_ms    = 0;
static int16_t  _touch_press_x     = 0;
static int16_t  _touch_press_y     = 0;
static int16_t  _touch_last_x      = 0;  // last tracked position while finger is down
static int16_t  _touch_last_y      = 0;

// ─────────────────── action queue (single-slot) ──────────
static volatile OnscreenAction _pending_action  = OnscreenAction::NONE;
static volatile char           _pending_kb_char = 0;

// ─────────────────── TFT + sprite ────────────────────────
static TFT_eSPI    _tft;
static TFT_eSprite _spr(&_tft);

// Draw location badge right-aligned in header.
// x_right = rightmost pixel the badge may use (default: SCR_W - 8).
// Dot position is computed dynamically from text width so nothing overlaps.
static void draw_location_badge(int x_right = SCR_W - 8) {
    if (_s_active_location.isEmpty()) return;
    String label = _s_active_location;
    if (label.length() > 18) label = label.substring(0, 18);
    uint16_t col = _s_location_color ? _s_location_color : C_ACCENT;

    _spr.setTextFont(2);
    _spr.setTextDatum(MR_DATUM);
    _spr.setTextColor(col, C_SURFACE);
    _spr.drawString(label.c_str(), x_right, HDR_H / 2);

    // Place dot just left of the text with a small gap
    int tw      = (int)_spr.textWidth(label.c_str());
    int dot_x   = x_right - tw - 10;
    _spr.fillCircle(dot_x, HDR_H / 2, 5, col);

    int badge_left = dot_x - 8;
    if (badge_left < 0) badge_left = 0;
    add_region(badge_left, 0, x_right - badge_left, HDR_H, OnscreenAction::LOCATION_BADGE);
}

// ─────────────────── home screen cache ───────────────────
struct HomeState {
    UiTab  tab;
    bool   wifiConnected;
    bool   sdMounted = false;
    String ssid, ip;
    String scannerStatus, scannerName;
    String lastScan, lastType;
    size_t inventoryCount = 0;
    int    expiringSoon   = 0;
    String message;
    bool   valid = false;
};
static HomeState _homeState;

// ═════════════════════════════════════════════════════════
//   Low-level drawing helpers
// ═════════════════════════════════════════════════════════

static String trunc(const String &s, int maxChars) {
    if ((int)s.length() <= maxChars) return s;
    return s.substring(0, maxChars - 1) + "~";
}

static void draw_button(int x, int y, int w, int h,
                        const char *label, uint16_t bg, uint16_t fg,
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

static void draw_card(int x, int y, int w, int h,
                      uint16_t bg = C_SURFACE, uint16_t border = C_BORDER) {
    _spr.fillRoundRect(x, y, w, h, 10, bg);
    _spr.drawRoundRect(x, y, w, h, 10, border);
}

static void draw_text(int x, int y, const char *text,
                      uint16_t color, uint8_t font, uint8_t datum = TL_DATUM) {
    _spr.setTextColor(color, C_BG);
    _spr.setTextFont(font);
    _spr.setTextDatum(datum);
    _spr.drawString(text, x, y);
}

static void draw_pill(int x, int y, const char *label, uint16_t bg) {
    _spr.setTextFont(2);
    int tw = _spr.textWidth(label) + 16;
    _spr.fillRoundRect(x, y, tw, 22, 11, bg);
    _spr.setTextColor(C_TEXT, bg);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(label, x + tw / 2, y + 11);
}

// ═════════════════════════════════════════════════════════
//   Header bar
// ═════════════════════════════════════════════════════════

static const char *_tab_labels[TAB_COUNT] = {
    "HOME", "INVENTAR", "SCANNER", "SYSTEM", "PRODUKTE", "EINGABE"
};
static const OnscreenAction _tab_actions[TAB_COUNT] = {
    OnscreenAction::TAB_STORE, OnscreenAction::TAB_INVENTORY,
    OnscreenAction::TAB_SCANNER, OnscreenAction::TAB_SYSTEM,
    OnscreenAction::TAB_MANUAL_PRODUCT, OnscreenAction::TAB_MANUAL_ENTRY,
};

static void draw_header(UiTab activeTab, bool wifiConnected) {
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    // Page name top-left
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(_tab_labels[static_cast<int>(activeTab)], 12, HDR_H / 2);
    // WiFi dot top-right
    uint16_t wc = wifiConnected ? C_GREEN : C_RED;
    _spr.fillCircle(SCR_W - 12, HDR_H / 2, 7, wc);
    _spr.drawCircle(SCR_W - 12, HDR_H / 2, 7, C_BORDER);
    _spr.setTextColor(C_SURFACE, wc);
    _spr.setTextFont(1);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("W", SCR_W - 12, HDR_H / 2);
    // Location badge left of WiFi dot
    draw_location_badge(SCR_W - 22); // leave room for WiFi dot (r=7 at SCR_W-12)
}


// ═════════════════════════════════════════════════════════
//   Content panels
// ═════════════════════════════════════════════════════════

static void draw_panel_store(const HomeState &s) {
    int py = CNT_Y;

    static const char   *stat_labels[4] = { "Produkte", "Ablaufend", "Kritisch", "Einkauf" };
    static const uint16_t stat_colors[4] = { C_ACCENT, C_YELLOW, C_RED, C_GREEN };
    int card_w = 115, card_h = 64;
    for (int i = 0; i < 4; i++) {
        int cx = 4 + i * (card_w + 4);
        int cy = py + 4;
        draw_card(cx, cy, card_w, card_h, C_SURFACE, stat_colors[i]);
        _spr.fillRect(cx + 1, cy + 1, 4, card_h - 2, stat_colors[i]);
        const char *count = (i == 0) ? String(s.inventoryCount).c_str() : "0";
        _spr.setTextColor(stat_colors[i], C_SURFACE);
        _spr.setTextFont(4);
        _spr.setTextDatum(TL_DATUM);
        _spr.drawString(count, cx + 10, cy + 8);
        _spr.setTextColor(C_SUBTEXT, C_SURFACE);
        _spr.setTextFont(2);
        _spr.drawString(stat_labels[i], cx + 10, cy + 38);
    }

    int pill_y = py + 72;
    uint16_t wifi_bg = s.wifiConnected ? C_GREEN : C_RED;
    bool ble_ok = (s.scannerStatus == "connected");
    uint16_t ble_bg = ble_ok ? C_GREEN : C_SURFACE2;

    _spr.setTextFont(2);
    String wifi_pill = s.wifiConnected ? "WLAN OK" : "WLAN FEHLT";
    draw_pill(4, pill_y, wifi_pill.c_str(), wifi_bg);
    int pill_x2 = 4 + (int)_spr.textWidth(wifi_pill.c_str()) + 20;
    draw_pill(pill_x2, pill_y, ble_ok ? "BLE OK" : "BLE ---", ble_ok ? C_GREEN : C_SURFACE2);

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

    draw_card(242, scan_y, 234, 72, C_SURFACE, C_YELLOW);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("BALD ABLAUFEND (7 Tage)", 250, scan_y + 6);
    _spr.setTextColor(C_YELLOW, C_SURFACE);
    if (s.expiringSoon > 0) {
        _spr.setTextFont(6);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(String(s.expiringSoon).c_str(), 258, scan_y + 48);
        _spr.setTextFont(2);
        _spr.setTextDatum(TL_DATUM);
        _spr.drawString("Artikel laufen ab", 258 + 30, scan_y + 40);
    } else {
        _spr.drawString("Alles im gruenen Bereich", 250, scan_y + 26);
    }

    int act_y = py + 178;
    draw_card(4, act_y, SCR_W - 8, 40, C_SURFACE, C_ACCENT);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("LETZTE AKTIVITAET", 12, act_y + 4);
    String msg = s.message.isEmpty() ? "System bereit" : trunc(s.message, 60);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.drawString(msg.c_str(), 12, act_y + 22);

    // QR code linking to mobile web app — only if IP is known
    if (s.wifiConnected && s.ip.length() > 0) {
        static constexpr int QR_VER  = 2;   // 25×25 modules
        static constexpr int QR_MOD  = 2;   // px per module
        static constexpr int QR_SIZE = 25 * QR_MOD; // 50px
        static constexpr int QR_PAD  = 3;   // quiet-zone padding (white border)
        int qr_box_w = QR_SIZE + QR_PAD * 2;
        int qr_box_h = QR_SIZE + QR_PAD * 2;
        int qr_x = SCR_W - qr_box_w - 4;
        int qr_y = act_y + 42;              // 2px gap below activity bar

        // White background (quiet zone)
        _spr.fillRect(qr_x, qr_y, qr_box_w, qr_box_h, TFT_WHITE);

        String url = "http://" + s.ip + "/m";
        QRCode qrcode;
        uint8_t qrData[qrcode_getBufferSize(QR_VER)];
        if (qrcode_initText(&qrcode, qrData, QR_VER, ECC_LOW, url.c_str()) == 0) {
            for (int my = 0; my < qrcode.size; my++) {
                for (int mx = 0; mx < qrcode.size; mx++) {
                    uint16_t col = qrcode_getModule(&qrcode, mx, my) ? TFT_BLACK : TFT_WHITE;
                    _spr.fillRect(qr_x + QR_PAD + mx * QR_MOD,
                                  qr_y + QR_PAD + my * QR_MOD,
                                  QR_MOD, QR_MOD, col);
                }
            }
        }

        // Label to the left of QR
        int lbl_x = 8;
        int lbl_y = qr_y;
        _spr.setTextColor(C_SUBTEXT, C_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(TL_DATUM);
        _spr.drawString("WEBINTERFACE", lbl_x, lbl_y + 4);
        _spr.setTextColor(C_ACCENT, C_BG);
        _spr.setTextFont(1);
        _spr.drawString(url.c_str(), lbl_x, lbl_y + 24);
        _spr.setTextColor(C_SUBTEXT, C_BG);
        _spr.drawString("QR scannen zum Oeffnen", lbl_x, lbl_y + 36);
    }
}

static void draw_panel_inventory_empty(const HomeState &s) {
    int py = CNT_Y;
    _spr.fillRect(0, py, SCR_W, 28, C_SURFACE2);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE2);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("Produkt", 8, py + 7);
    _spr.drawString("MHD",    300, py + 7);
    _spr.drawString("Menge",  420, py + 7);
    _spr.drawFastHLine(0, py + 28, SCR_W, C_BORDER);
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("Keine Eintraege", SCR_W / 2, py + 80);
    String count_str = String(s.inventoryCount) + " Eintraege";
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString(count_str.c_str(), 8, CNT_Y + CNT_H - 18);
}

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

static void draw_panel_system(const HomeState &s) {
    int py = CNT_Y;
    draw_card(4, py + 8, 230, 120, C_SURFACE, C_GREEN);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("NETZWERK", 12, py + 14);
    uint16_t wc = s.wifiConnected ? C_GREEN : C_RED;
    _spr.setTextColor(wc, C_SURFACE);
    _spr.setTextFont(4);
    _spr.drawString(s.wifiConnected ? "Verbunden" : "Nicht verbunden", 12, py + 34);
    String ssid_str = s.wifiConnected ? trunc(s.ssid, 22) : "Kein WLAN";
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.drawString(ssid_str.c_str(), 12, py + 66);
    String ip_str = s.ip.isEmpty() ? "" : s.ip;
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.drawString(ip_str.c_str(), 12, py + 84);
    draw_button(12, py + 100, 210, 30,
                "WLAN einrichten", C_YELLOW, C_BG, 2, OnscreenAction::WIFI_SETUP);
    draw_card(242, py + 8, 234, 120, C_SURFACE, C_ACCENT);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("GERAET", 250, py + 14);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.drawString("FoodScanner ESP32-S3", 250, py + 34);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.drawString("ST7796 | FT6336 | 480x320", 250, py + 54);
    _spr.setTextColor(s.sdMounted ? C_GREEN : C_SUBTEXT, C_SURFACE);
    _spr.drawString(s.sdMounted ? "SD: eingelegt" : "SD: nicht eingelegt", 250, py + 74);

    // Scanner card below
    bool ble_ok = (s.scannerStatus == "connected");
    draw_card(4, py + 136, SCR_W - 8, 112, C_SURFACE, C_YELLOW);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("BLE-HID BARCODE SCANNER", 12, py + 142);
    uint16_t sc = ble_ok ? C_GREEN : C_YELLOW;
    _spr.setTextColor(sc, C_SURFACE);
    _spr.setTextFont(4);
    _spr.drawString(ble_ok ? "Verbunden" : "Getrennt", 12, py + 160);
    String name_str = s.scannerName.isEmpty() ? "Koppeln im Web-UI" : trunc(s.scannerName, 36);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.drawString(name_str.c_str(), 12, py + 196);
    draw_button(SCR_W - 222, py + 186, 210, 34,
                "Verbinden / Trennen", C_ACCENT, C_BG, 2, OnscreenAction::SCANNER_RECONNECT);
}

static void draw_panel_manual_product(const HomeState &) {
    int py = CNT_Y;
    _spr.setTextColor(C_ACCENT, C_BG);
    _spr.setTextFont(4);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Produkt auswaehlen", SCR_W / 2, py + 16);
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.drawString("Vorlagen im Web-UI anlegen", SCR_W / 2, py + 54);
    draw_button(SCR_W / 2 - 140, py + 90, 280, 48,
                "Kategorie auswaehlen", C_ACCENT, C_BG, 2,
                OnscreenAction::TAB_MANUAL_PRODUCT);
}

static void draw_panel_manual_entry(const HomeState &) {
    int py = CNT_Y;
    _spr.setTextColor(C_ACCENT, C_BG);
    _spr.setTextFont(4);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("MANUELL EINGEBEN", SCR_W / 2, py + 16);
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.drawString("Produktname per Tastatur eingeben", SCR_W / 2, py + 54);
    draw_button(SCR_W / 2 - 120, py + 90, 240, 48,
                "Eingabe starten", C_GREEN, C_BG, 2,
                OnscreenAction::KB_CONFIRM);
}

// ═════════════════════════════════════════════════════════
//   Commit sprite to display
// ═════════════════════════════════════════════════════════

static void commit() { _spr.pushSprite(0, 0); }

// ═════════════════════════════════════════════════════════
//   Home renderer
// ═════════════════════════════════════════════════════════

static void render_home(const HomeState &s) {
    _spr.fillSprite(C_BG);
    clear_regions();
    draw_header(s.tab, s.wifiConnected);
    _spr.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG);
    switch (s.tab) {
        case UiTab::STORE:          draw_panel_store(s);          break;
        case UiTab::INVENTORY:      draw_panel_inventory_empty(s); break;
        case UiTab::SCANNER:        draw_panel_system(s);         break;
        case UiTab::SYSTEM:         draw_panel_system(s);         break;
        case UiTab::MANUAL_PRODUCT: draw_panel_manual_product(s); break;
        case UiTab::MANUAL_ENTRY:   draw_panel_manual_entry(s);   break;
    }
    commit();
}

// ═════════════════════════════════════════════════════════
//   Display class
// ═════════════════════════════════════════════════════════

Display display_obj;
Display::Display() {}
Display::~Display() {}

void Display::init() {
    if (_initialized) return;
    _tft.init();
    _tft.setRotation(1);
    _tft.invertDisplay(true);
    _tft.fillScreen(TFT_BLACK);
    _spr.setColorDepth(16);
    if (!_spr.createSprite(SCR_W, SCR_H)) {
        Serial.println("[Display] PSRAM sprite failed – using internal RAM");
        _spr.createSprite(SCR_W, SCR_H);
    }
    _spr.setSwapBytes(true);
    _initialized = true;
    Serial.printf("[Display] TFT_eSPI ready %d x %d\n", SCR_W, SCR_H);
}

// ─────────────────── touch + swipe ───────────────────────

void Display::tick() {
    if (!_initialized) return;
    TouchPoint tp = touch_obj.read();

    if (tp.pressed) {
        if (!_touch_was_pressed) {
            _touch_was_pressed = true;
            _touch_press_ms    = millis();
            _touch_press_x     = tp.x;
            _touch_press_y     = tp.y;
            _touch_last_x      = tp.x;
            _touch_last_y      = tp.y;
        } else {
            // Track finger movement while pressed for swipe detection
            _touch_last_x = tp.x;
            _touch_last_y = tp.y;
        }
    } else {
        if (_touch_was_pressed) {
            uint32_t held = millis() - _touch_press_ms;
            if (held >= 50) {
                int16_t px = _touch_press_x, py = _touch_press_y;
                int16_t dx = _touch_last_x - px;
                int16_t dy = _touch_last_y - py;

                portENTER_CRITICAL(&_regions_mux);
                int count = _region_count;
                HitRegion snap[MAX_REGIONS];
                memcpy(snap, _regions, count * sizeof(HitRegion));
                portEXIT_CRITICAL(&_regions_mux);

                // Swipe detection
                bool is_h_swipe = (abs(dx) > 80) && (abs(dx) > abs(dy) * 2);
                bool is_v_swipe_down = (dy > 80) && (dy > abs(dx) * 2);

                OnscreenAction hit = OnscreenAction::NONE;
                char           hit_char = 0;

                if (is_h_swipe && py > HDR_H && py < SCR_H - TAB_H) {
                    hit = (dx < 0) ? OnscreenAction::SWIPE_LEFT : OnscreenAction::SWIPE_RIGHT;
                } else if (is_v_swipe_down && py < HDR_H + 60) {
                    hit = OnscreenAction::SWIPE_DOWN;
                } else {
                    // Hit-test using press start coordinates
                    for (int i = 0; i < count; i++) {
                        if (px >= snap[i].x && px < snap[i].x + snap[i].w &&
                            py >= snap[i].y && py < snap[i].y + snap[i].h) {
                            hit      = snap[i].action;
                            hit_char = snap[i].extra_char;
                            break;
                        }
                    }
                }

                if (hit != OnscreenAction::NONE) {
                    _pending_kb_char = hit_char;
                    _pending_action  = hit;
                }
            }
            _touch_was_pressed = false;
        }
    }
}

static void touch_task_fn(void * /*param*/) {
    for (;;) {
        display_obj.tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Display::startTouchTask() {
    xTaskCreatePinnedToCore(touch_task_fn, "touch_poll", 4096, nullptr, 5, nullptr, 0);
}

OnscreenAction Display::hitTest(uint16_t /*x*/, uint16_t /*y*/) const {
    OnscreenAction a = _pending_action;
    _pending_action  = OnscreenAction::NONE;
    return a;
}

char Display::drainKbChar() {
    char c = _pending_kb_char;
    _pending_kb_char = 0;
    return c;
}

void Display::fillScreen(uint16_t color) { _tft.fillScreen(color); }
void Display::drawText(int x, int y, const char *text, uint16_t color) {
    _tft.setTextColor(color);
    _tft.drawString(text, x, y);
}
void Display::clear() {
    if (_initialized) { _spr.fillSprite(C_BG); commit(); }
}

// ─────────────────────────────────────────────────────────
//   Splash
// ─────────────────────────────────────────────────────────

void Display::showSplash() {
    if (!_initialized) return;
    clear_regions();
    _spr.fillSprite(C_BG);
    int bw = 360, bh = 160;
    int bx = (SCR_W - bw) / 2, by = (SCR_H - bh) / 2 - 10;
    _spr.fillRoundRect(bx, by, bw, bh, 16, C_SURFACE);
    _spr.drawRoundRect(bx, by, bw, bh, 16, C_ACCENT);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(6);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("FoodScanner", SCR_W / 2, by + 20);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Lebensmittel smart verwalten", SCR_W / 2, by + 86);
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(BC_DATUM);
    _spr.drawString("System startet...", SCR_W / 2, SCR_H - 16);
    commit();
}

// ─────────────────────────────────────────────────────────
//   WiFi scan / setup screen
// ─────────────────────────────────────────────────────────

void Display::showWifiScan() {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();
    _spr.setTextColor(C_ACCENT, C_BG);
    _spr.setTextFont(4);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("WLAN-Einrichtung", SCR_W / 2, SCR_H / 2 - 36);
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Netzwerke werden gesucht\x85", SCR_W / 2, SCR_H / 2 + 4);
    commit();
}

// ─────────────────────────────────────────────────────────
//   WiFi / Dashboard delegates
// ─────────────────────────────────────────────────────────

void Display::showWiFiStatus(const String &ssid, const String &ip, bool connected) {
    showHome(UiTab::SYSTEM, ssid, ip, connected, "", "", "", "", 0, 0,
             connected ? "WLAN verbunden" : "Setup-AP aktiv");
}

void Display::showDashboard(const String &ssid, const String &ip, bool wifiConnected,
                             const String &scannerStatus, const String &scannerName,
                             const String &lastScan, const String &lastType,
                             const String &message) {
    showHome(UiTab::STORE, ssid, ip, wifiConnected, scannerStatus, scannerName,
             lastScan, lastType, _homeState.inventoryCount, _homeState.expiringSoon, message);
}

void Display::showHome(UiTab activeTab,
                       const String &ssid, const String &ip, bool wifiConnected,
                       const String &scannerStatus, const String &scannerName,
                       const String &lastScan, const String &lastType,
                       size_t inventoryCount, int expiringSoon,
                       const String &message, bool sdMounted) {
    if (!_initialized) return;
    _homeState.tab            = activeTab;
    _homeState.wifiConnected  = wifiConnected;
    _homeState.sdMounted      = sdMounted;
    _homeState.ssid           = ssid;
    _homeState.ip             = ip;
    _homeState.scannerStatus  = scannerStatus;
    _homeState.scannerName    = scannerName;
    _homeState.lastScan       = lastScan;
    _homeState.lastType       = lastType;
    _homeState.inventoryCount = inventoryCount;
    _homeState.expiringSoon   = expiringSoon;
    _homeState.message        = message;
    _homeState.valid          = true;
    render_home(_homeState);
}

// ─────────────────────────────────────────────────────────
//   Fetching overlay
// ─────────────────────────────────────────────────────────

void Display::showFetchingProduct(const String &barcode) {
    if (!_initialized) return;
    if (_homeState.valid) render_home(_homeState);
    else _spr.fillSprite(C_BG);
    int dw = 380, dh = 130;
    int dx = (SCR_W - dw) / 2, dy = (SCR_H - dh) / 2;
    _spr.fillRoundRect(dx, dy, dw, dh, 12, C_SURFACE);
    _spr.drawRoundRect(dx, dy, dw, dh, 12, C_ACCENT);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Open Food Facts - Suche...", dx + dw / 2, dy + 14);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString(trunc(barcode, 24).c_str(), dx + dw / 2, dy + 44);
    for (int i = 0; i < 3; i++)
        _spr.fillCircle(dx + dw / 2 - 16 + i * 16, dy + 92, 5, C_ACCENT);
    clear_regions();
    draw_button(dx + dw - 140, dy + dh - 44, 124, 34,
                "Abbrechen", C_RED, C_TEXT, 2, OnscreenAction::CANCEL);
    commit();
}

// ─────────────────────────────────────────────────────────
//   Date entry numpad
// ─────────────────────────────────────────────────────────

void Display::showDateEntry(const ProductInfo &product, const String &dateDraft) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // ── Header ────────────────────────────────────────────────
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextDatum(ML_DATUM);
    _spr.setTextFont(4);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.drawString("MHD EINGEBEN", 10, HDR_H / 2);
    draw_location_badge();

    // ── Vertical divider between info panel and numpad ────────
    static constexpr int DIV_X = 240;
    _spr.drawFastVLine(DIV_X, HDR_H, CNT_H, C_BORDER);

    // ── Left panel: product info + date display ───────────────
    static constexpr int LP_PAD = 10;
    static constexpr int LP_W   = DIV_X - 1;

    // Product name
    _spr.setTextFont(2);
    _spr.setTextColor(C_TEXT, C_BG);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(trunc(product.name,   24).c_str(), LP_PAD, HDR_H + 18);
    _spr.setTextColor(RGB(0x80,0x90,0xA0), C_BG);
    _spr.drawString(trunc(product.brand,  24).c_str(), LP_PAD, HDR_H + 38);

    // Separator
    _spr.drawFastHLine(LP_PAD, HDR_H + 54, LP_W - LP_PAD * 2, C_BORDER);

    // "MHD Eingabe:" label
    _spr.setTextFont(2);
    _spr.setTextColor(RGB(0x80,0x90,0xA0), C_BG);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("MHD Eingabe:", LP_PAD, HDR_H + 70);

    // Date display card
    static constexpr int BOX_X = LP_PAD;
    static constexpr int BOX_Y = HDR_H + 84;
    static constexpr int BOX_W = LP_W - LP_PAD * 2;
    static constexpr int BOX_H = 96;
    draw_card(BOX_X, BOX_Y, BOX_W, BOX_H, C_SURFACE, C_ACCENT);

    // Date display with TT.MM.JJ placeholder
    {
        static const char PLACEHOLDER[6] = {'T','T','M','M','J','J'};
        String fmt;
        for (int i = 0; i < 6; i++) {
            fmt += (i < (int)dateDraft.length()) ? dateDraft[i] : PLACEHOLDER[i];
            if (i == 1 || i == 3) fmt += '.';
        }
        _spr.setTextFont(6);
        _spr.setTextColor(C_ACCENT, C_SURFACE);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(fmt.c_str(), BOX_X + BOX_W / 2, BOX_Y + BOX_H / 2);
    }

    // Cancel button pinned to bottom of left panel
    draw_button(LP_PAD, SCR_H - 48, LP_W - LP_PAD * 2, 42,
                "Abbrechen", C_YELLOW, C_BG, 4, OnscreenAction::CANCEL);

    // ── Right panel: numpad with smart key dimming ────────────
    static constexpr int NP_X   = DIV_X + 1;
    static constexpr int NP_W   = SCR_W - NP_X;          // 239
    static constexpr int NP_Y   = HDR_H;
    static constexpr int NP_H   = CNT_H;                  // 276
    static constexpr int COLS   = 3;
    static constexpr int ROWS   = 4;
    static constexpr int BTN_W  = NP_W / COLS;            // 79
    static constexpr int BTN_H  = NP_H / ROWS;            // 69

    // Compute bitmask of valid digits for current position (bit N = digit N allowed)
    uint16_t valid = 0x3FF;  // all digits 0-9 by default
    int pos = (int)dateDraft.length();
    if (pos == 0) {                          // day tens: 0-3
        valid = 0x000F;
    } else if (pos == 1) {                   // day units
        char d0 = dateDraft[0];
        if      (d0 == '0') valid = 0x01FE;  // 1-9  (no 00)
        else if (d0 == '3') valid = 0x0003;  // 0,1  (30,31)
        else                valid = 0x03FF;  // 0-9
    } else if (pos == 2) {                   // month tens: 0-1
        valid = 0x0003;
    } else if (pos == 3) {                   // month units
        char d2 = dateDraft[2];
        if (d2 == '0') valid = 0x01FE;       // 1-9  (no month 00)
        else           valid = 0x0007;       // 0,1,2 (10,11,12)
    }
    // positions 4,5 (year): all digits valid

    static const char *NUM_LABELS[9] = {"1","2","3","4","5","6","7","8","9"};
    static const OnscreenAction NUM_ACT[9] = {
        OnscreenAction::DATE_DIGIT_1, OnscreenAction::DATE_DIGIT_2, OnscreenAction::DATE_DIGIT_3,
        OnscreenAction::DATE_DIGIT_4, OnscreenAction::DATE_DIGIT_5, OnscreenAction::DATE_DIGIT_6,
        OnscreenAction::DATE_DIGIT_7, OnscreenAction::DATE_DIGIT_8, OnscreenAction::DATE_DIGIT_9,
    };

    // Rows 0–2: digits 1–9
    for (int i = 0; i < 9; i++) {
        int digit = i + 1;
        bool enabled = (valid >> digit) & 1;
        int row = i / COLS, col = i % COLS;
        int bx = NP_X + col * BTN_W;
        int by = NP_Y + row * BTN_H;
        uint16_t bg = enabled ? C_SURFACE2 : C_SURFACE;
        uint16_t fg = enabled ? C_TEXT     : C_SUBTEXT;
        _spr.fillRect(bx, by, BTN_W, BTN_H, bg);
        _spr.drawFastHLine(bx, by, BTN_W, C_BORDER);
        _spr.drawFastVLine(bx, by, BTN_H, C_BORDER);
        _spr.setTextFont(6);
        _spr.setTextColor(fg, bg);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(NUM_LABELS[i], bx + BTN_W / 2, by + BTN_H / 2);
        if (enabled) add_region(bx, by, BTN_W, BTN_H, NUM_ACT[i]);
    }

    // Row 3: [0 — double width] [← — red]
    int row3_y = NP_Y + 3 * BTN_H;
    int zero_w = BTN_W * 2;
    bool zero_ok = (valid >> 0) & 1;

    uint16_t zero_bg = zero_ok ? C_SURFACE2 : C_SURFACE;
    uint16_t zero_fg = zero_ok ? C_TEXT     : C_SUBTEXT;
    _spr.fillRect(NP_X,          row3_y, zero_w, BTN_H, zero_bg);
    _spr.fillRect(NP_X + zero_w, row3_y, BTN_W,  BTN_H, C_RED);
    _spr.drawFastHLine(NP_X, row3_y, NP_W, C_BORDER);
    _spr.drawFastVLine(NP_X,          row3_y, BTN_H, C_BORDER);
    _spr.drawFastVLine(NP_X + zero_w, row3_y, BTN_H, C_BORDER);

    _spr.setTextFont(6);
    _spr.setTextColor(zero_fg, zero_bg);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("0", NP_X + zero_w / 2, row3_y + BTN_H / 2);

    _spr.setTextFont(4);
    _spr.setTextColor(C_TEXT, C_RED);
    _spr.drawString("<--", NP_X + zero_w + BTN_W / 2, row3_y + BTN_H / 2);

    if (zero_ok) add_region(NP_X, row3_y, zero_w, BTN_H, OnscreenAction::DATE_DIGIT_0);
    add_region(NP_X + zero_w, row3_y, BTN_W, BTN_H, OnscreenAction::DATE_BACKSPACE);

    commit();
}

// ─────────────────────────────────────────────────────────
//   Quantity entry
// ─────────────────────────────────────────────────────────

// ── Quantity grid helper (shared by showQuantityEntry and showTemplateMHD) ──
// Draws a 4-column × 3-row grid of quantity buttons (1–12).
// Highlighted button = current quantity. btn_h controls button height.
static void draw_qty_grid(int top_y, int selected, int btn_h = 52) {
    static const OnscreenAction QTY_ACTIONS[12] = {
        OnscreenAction::QTY_1,  OnscreenAction::QTY_2,  OnscreenAction::QTY_3,
        OnscreenAction::QTY_4,  OnscreenAction::QTY_5,  OnscreenAction::QTY_6,
        OnscreenAction::QTY_7,  OnscreenAction::QTY_8,  OnscreenAction::QTY_9,
        OnscreenAction::QTY_10, OnscreenAction::QTY_11, OnscreenAction::QTY_12,
    };
    const int COLS = 4, ROWS = 3;
    const int BTN_W = 108, GAP = 6;
    const int grid_w = COLS * BTN_W + (COLS - 1) * GAP;
    const int x0 = (SCR_W - grid_w) / 2;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int n = r * COLS + c + 1;  // 1..12
            int bx = x0 + c * (BTN_W + GAP);
            int by = top_y + r * (btn_h + GAP);
            bool sel = (n == selected);
            uint16_t bg = sel ? C_ACCENT : C_SURFACE2;
            uint16_t fg = sel ? C_BG     : C_TEXT;
            draw_button(bx, by, BTN_W, btn_h, String(n).c_str(), bg, fg, 4, QTY_ACTIONS[n - 1]);
        }
    }
}

void Display::showQuantityEntry(const ProductInfo &product,
                                 const String &expiryDate, int quantity) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // Header: product name + MHD
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(trunc(product.name, 22).c_str(), 8, HDR_H / 2);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextDatum(MR_DATUM);
    _spr.drawString(("MHD " + expiryDate).c_str(), SCR_W - 8, HDR_H / 2);

    // Subheader hint
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Menge 2x antippen = Einlagern", SCR_W / 2, HDR_H + 6);

    // 4×3 quantity grid — taller buttons fill the available space
    // Available: SCR_H - HDR_H - 28(hint) = 248px for 3 rows + 2 gaps (6px)
    // btn_h = (248 - 2*6) / 3 = 78px
    static constexpr int GRID_TOP = HDR_H + 26;
    static constexpr int BTN_H    = 78;
    draw_qty_grid(GRID_TOP, quantity, BTN_H);

    // Swipe-right hint at bottom
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(1);
    _spr.setTextDatum(BC_DATUM);
    _spr.drawString("< wischen zum Abbrechen", SCR_W / 2, SCR_H - 2);

    commit();
}

// ─────────────────────────────────────────────────────────
//   Result dialog
// ─────────────────────────────────────────────────────────

void Display::showResult(const String &title, const String &message, bool success) {
    if (!_initialized) return;
    if (_homeState.valid) render_home(_homeState);
    else _spr.fillSprite(C_BG);
    int dw = 380, dh = 160;
    int dx = (SCR_W - dw) / 2, dy = (SCR_H - dh) / 2;
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
//   Inventory list
// ─────────────────────────────────────────────────────────

void Display::showInventoryList(const std::vector<InventoryItem> &items,
                                const String &filter, const String &hhAbbr) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // ── Header ──────────────────────────────────────────────
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("INVENTAR", 12, HDR_H / 2);
    draw_location_badge();

    // ── Search bar ──────────────────────────────────────────
    static constexpr int SEARCH_H = 30;
    int sb_y = HDR_H;
    uint16_t sb_bg = RGB(0x0E, 0x14, 0x1C);
    _spr.fillRect(0, sb_y, SCR_W, SEARCH_H, sb_bg);
    _spr.drawRoundRect(6, sb_y + 4, SCR_W - 12, SEARCH_H - 8, 4, C_BORDER);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    if (filter.isEmpty()) {
        _spr.setTextColor(C_SUBTEXT, sb_bg);
        _spr.drawString("[ Suchen... ]", 14, sb_y + SEARCH_H / 2);
    } else {
        _spr.setTextColor(C_TEXT, sb_bg);
        _spr.drawString(("> " + filter).c_str(), 14, sb_y + SEARCH_H / 2);
    }
    _spr.drawFastHLine(0, sb_y + SEARCH_H - 1, SCR_W, C_BORDER);
    add_region(0, sb_y, SCR_W, SEARCH_H, OnscreenAction::INV_SEARCH);

    // ── Column headers ──────────────────────────────────────
    static constexpr int COL_H = 24;
    int col_y = HDR_H + SEARCH_H;
    _spr.fillRect(0, col_y, SCR_W, COL_H, C_SURFACE2);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE2);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("Produkt", 8,   col_y + 5);
    _spr.drawString("MHD",    310,  col_y + 5);
    _spr.drawString("Menge",  432,  col_y + 5);
    _spr.drawFastHLine(0, col_y + COL_H - 1, SCR_W, C_BORDER);

    // ── Item rows ───────────────────────────────────────────
    static constexpr int ROW_H = 40, MAX_ROWS = 5;
    int list_y = HDR_H + SEARCH_H + COL_H, shown = 0;
    int start = static_cast<int>(items.size()) - 1;
    for (int i = start; i >= 0 && shown < MAX_ROWS; i--, shown++) {
        const InventoryItem &item = items[i];
        int ry = list_y + shown * ROW_H;
        uint16_t row_bg = (shown % 2 == 0) ? C_SURFACE : C_SURFACE2;
        _spr.fillRect(0, ry, SCR_W, ROW_H, row_bg);

        // Name line (font 2, up to 26 chars)
        _spr.setTextColor(C_TEXT, row_bg);
        _spr.setTextFont(2);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(trunc(item.name, 26).c_str(), 8, ry + 11);

        // Sub-line: household abbr + location (font 1, ASCII only)
        String sub;
        if (!hhAbbr.isEmpty()) sub = hhAbbr + " ";
        if (!item.location.isEmpty()) sub += "| " + item.location;
        if (!sub.isEmpty()) {
            _spr.setTextColor(C_SUBTEXT, row_bg);
            _spr.setTextFont(1);
            _spr.setTextDatum(ML_DATUM);
            _spr.drawString(trunc(sub, 36).c_str(), 8, ry + 28);
        }

        // MHD + Qty
        _spr.setTextColor(C_TEXT, row_bg);
        _spr.setTextFont(2);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(trunc(item.expiryDate, 10).c_str(), 310, ry + ROW_H / 2);
        _spr.setTextColor(C_ACCENT, row_bg);
        _spr.drawString(String(item.quantity).c_str(), 432, ry + ROW_H / 2);
        _spr.drawFastHLine(0, ry + ROW_H - 1, SCR_W, C_BORDER);
    }
    if (items.empty()) {
        _spr.setTextColor(C_SUBTEXT, C_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(filter.isEmpty() ? "Keine Eintraege" : "Keine Treffer",
                        SCR_W / 2, list_y + 60);
    }
    _homeState.inventoryCount = items.size();
    commit();
}

// ─────────────────────────────────────────────────────────
//   Category tile grid  (2 cols × 4 rows, max 7 tiles)
// ─────────────────────────────────────────────────────────

static const OnscreenAction LIST_ACTIONS[7] = {
    OnscreenAction::LIST_ITEM_0, OnscreenAction::LIST_ITEM_1,
    OnscreenAction::LIST_ITEM_2, OnscreenAction::LIST_ITEM_3,
    OnscreenAction::LIST_ITEM_4, OnscreenAction::LIST_ITEM_5,
    OnscreenAction::LIST_ITEM_6,
};

static const uint16_t TILE_ACCENTS[7] = {
    RGB(0x4C,0x9E,0xFF), // blue
    RGB(0x2E,0xB0,0x48), // green
    RGB(0xCC,0x92,0x18), // yellow
    RGB(0xE0,0x78,0x18), // orange
    RGB(0xA0,0x60,0xE0), // purple
    RGB(0x10,0xB4,0xA0), // teal
    RGB(0xF0,0x46,0x40), // red
};

void Display::showCategoryTiles(const std::vector<String> &categories) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // Header
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("KATEGORIEN", 12, HDR_H / 2);
    draw_location_badge();

    // 2 columns × 4 rows, content area 480×276
    static constexpr int COLS     = 2;
    static constexpr int TILE_W   = 228;  // (480 - 3*8) / 2
    static constexpr int TILE_H   = 63;   // (276 - 6 - 3*6) / 4
    static constexpr int GAP_X    = 8;
    static constexpr int GAP_Y    = 6;
    static constexpr int MARGIN_X = 8;
    static constexpr int MARGIN_Y = 6;
    static constexpr int MAX_SHOW = 7;

    int shown = (int)categories.size() < MAX_SHOW ? (int)categories.size() : MAX_SHOW;

    for (int i = 0; i < shown; i++) {
        int col   = i % COLS;
        int row   = i / COLS;
        int tx    = MARGIN_X + col * (TILE_W + GAP_X);
        int ty    = HDR_H + MARGIN_Y + row * (TILE_H + GAP_Y);
        uint16_t accent = TILE_ACCENTS[i % 7];

        // Tile background with colored border
        _spr.fillRoundRect(tx, ty, TILE_W, TILE_H, 10, C_SURFACE);
        _spr.drawRoundRect(tx, ty, TILE_W, TILE_H, 10, accent);
        _spr.drawRoundRect(tx + 1, ty + 1, TILE_W - 2, TILE_H - 2, 9, accent); // 2px border

        // Left accent bar
        _spr.fillRoundRect(tx + 1, ty + 1, 5, TILE_H - 2, 9, accent);

        // Category name — centered in tile (offset right of bar)
        _spr.setTextFont(4);
        _spr.setTextColor(C_TEXT, C_SURFACE);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(trunc(categories[i], 20).c_str(), tx + 14, ty + TILE_H / 2);

        add_region(tx, ty, TILE_W, TILE_H, LIST_ACTIONS[i]);
    }

    if (categories.empty()) {
        _spr.setTextColor(C_SUBTEXT, C_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString("Keine Kategorien – Vorlagen im Web-UI anlegen", SCR_W / 2, HDR_H + 80);
    }

    commit();
}

// ─────────────────────────────────────────────────────────
//   Product list screen  (taller rows, larger font)
// ─────────────────────────────────────────────────────────

void Display::showListScreen(const char *title,
                              const std::vector<String> &items,
                              bool showBack) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // Header
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(title, 12, HDR_H / 2);
    draw_location_badge();

    if (showBack) {
        _spr.setTextColor(C_SUBTEXT, C_SURFACE);
        _spr.setTextFont(2);
        _spr.setTextDatum(MR_DATUM);
        _spr.drawString("< wischen = zur\xFC" "ck", SCR_W - 8 - (int)(_s_active_location.isEmpty() ? 0 : 115), HDR_H / 2);
    }

    // Taller rows for comfortable finger tapping and larger text
    static constexpr int ITEM_H   = 55;  // 5 rows × 55px = 275px ≈ 276px content
    static constexpr int MAX_SHOW = 5;
    int list_y = HDR_H;
    int shown  = (int)items.size() < MAX_SHOW ? (int)items.size() : MAX_SHOW;

    for (int i = 0; i < shown; i++) {
        int iy = list_y + i * ITEM_H;
        uint16_t bg = (i % 2 == 0) ? C_SURFACE : C_SURFACE2;
        _spr.fillRect(0, iy, SCR_W, ITEM_H, bg);

        // Left accent stripe
        _spr.fillRect(0, iy, 3, ITEM_H, C_ACCENT);

        _spr.setTextColor(C_TEXT, bg);
        _spr.setTextFont(4);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(trunc(items[i], 38).c_str(), 12, iy + ITEM_H / 2);

        // Chevron
        _spr.setTextColor(C_ACCENT, bg);
        _spr.setTextDatum(MR_DATUM);
        _spr.drawString(">", SCR_W - 12, iy + ITEM_H / 2);
        _spr.drawFastHLine(0, iy + ITEM_H - 1, SCR_W, C_BORDER);
        add_region(0, iy, SCR_W, ITEM_H, LIST_ACTIONS[i]);
    }

    if (items.empty()) {
        _spr.setTextColor(C_SUBTEXT, C_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString("Keine Eintraege – Vorlagen im Web-UI anlegen", SCR_W / 2, HDR_H + 80);
    }

    commit();
}

// ─────────────────────────────────────────────────────────
//   Template MHD + qty confirm
// ─────────────────────────────────────────────────────────

void Display::showTemplateMHD(const String &productName,
                               const String &mhd, int qty) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // Header
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(trunc(productName, 22).c_str(), 8, HDR_H / 2);
    draw_location_badge();

    // MHD display (no day-adjust buttons)
    int mhd_y = HDR_H + 6;
    _spr.setTextColor(C_SUBTEXT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("MHD:", 8, mhd_y + 8);
    _spr.setTextColor(C_ACCENT, C_BG);
    _spr.setTextFont(4);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(mhd.c_str(), SCR_W / 2, mhd_y + 16);

    // Separator
    _spr.drawFastHLine(0, mhd_y + 38, SCR_W, C_BORDER);

    // Subheader for qty
    _spr.setTextColor(C_ACCENT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Menge ausw\xE4hlen", SCR_W / 2, mhd_y + 44);

    // 4×3 quantity grid
    draw_qty_grid(mhd_y + 62, qty);

    // Bottom buttons
    int bot_y = SCR_H - 52;
    draw_button(4,            bot_y, 130, 44, "Abbrechen",   C_RED,   C_TEXT, 2, OnscreenAction::CANCEL);
    draw_button(SCR_W - 180, bot_y, 170, 44, "Einlagern ->", C_GREEN, C_BG,   2, OnscreenAction::MHD_CONFIRM);

    commit();
}

// ─────────────────────────────────────────────────────────
//   On-screen keyboard
// ─────────────────────────────────────────────────────────
// Layout: 480×232 (below 44px header + 44px input display)
// Rows 0-2: 10 keys × 48px wide × 58px tall
// Row 3: CAPS(96) + SPACE(192) + OK(192) = 480px

static const char KB_NUM_NORM[10]  = {'1','2','3','4','5','6','7','8','9','0'};
static const char KB_NUM_SHIFT[10] = {'!','@','#','$','%','&','*','(',')','?' };
static const char KB_KEYS[3][10] = {
    {'Q','W','E','R','T','Z','U','I','O','P'},
    {'A','S','D','F','G','H','J','K','L','.'},
    {'Y','X','C','V','B','N','M','-','_', 0 },  // 0 = backspace slot
};

void Display::kbReset() {
    _kbShift = true;
    _kbCaps  = false;
}

void Display::kbAutoShift(char c) {
    if (c == ' ') {
        _kbShift = true;   // auto-uppercase after space
    } else {
        _kbShift = _kbCaps;  // revert to caps-lock state
    }
}

void Display::kbToggleCaps() {
    _kbCaps  = !_kbCaps;
    _kbShift = _kbCaps;
}

void Display::showKeyboardEntry(const String &title, const String &current) {
    if (!_initialized) return;

    _spr.fillSprite(C_BG);
    clear_regions();

    // ── Layout: 480×320 ──────────────────────────────────────────
    // Header     44 px  (y=0)
    // Input bar  36 px  (y=44)
    // Num row    48 px  (y=80)   – digits / symbols via shift
    // Letter×3   48 px each      (y=128/176/224)
    // Bottom row 48 px  (y=272)  – CAPS | SPACE | OK
    static constexpr int KEY_W  = 48;
    static constexpr int KEY_H  = 48;
    static constexpr int INP_H  = 36;
    static constexpr int NUM_Y  = HDR_H + INP_H;       // 80
    static constexpr int KB_Y   = NUM_Y + KEY_H;       // 128
    static constexpr int BOT_Y  = KB_Y + 3 * KEY_H;   // 272

    // ── Header ───────────────────────────────────────────────────
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(title, 8, HDR_H / 2);

    // ── Input bar ────────────────────────────────────────────────
    _spr.fillRect(0, HDR_H, SCR_W, INP_H, C_SURFACE2);
    _spr.drawFastHLine(0, HDR_H + INP_H - 1, SCR_W, C_BORDER);
    String disp = current.isEmpty() ? "_ _ _" : (trunc(current, 38) + "_");
    _spr.setTextColor(C_TEXT, C_SURFACE2);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(disp.c_str(), 8, HDR_H + INP_H / 2);

    // ── Helper lambda: draw one key ───────────────────────────────
    auto draw_key = [&](int bx, int by, int w, int h,
                        const char *label, uint16_t bg, uint16_t fg,
                        OnscreenAction act, char reg = 0) {
        _spr.fillRect(bx, by, w, h, bg);
        _spr.drawFastVLine(bx, by, h, C_BORDER);
        _spr.drawFastHLine(bx, by, w, C_BORDER);
        _spr.setTextColor(fg, bg);
        _spr.setTextFont(2);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(label, bx + w / 2, by + h / 2);
        add_region(bx, by, w, h, act, reg);
    };

    // ── Number / symbol row (shift toggles 123 ↔ !@#) ───────────
    for (int col = 0; col < 10; col++) {
        char c = _kbShift ? KB_NUM_SHIFT[col] : KB_NUM_NORM[col];
        char lbuf[3] = {c, 0, 0};
        draw_key(col * KEY_W, NUM_Y, KEY_W, KEY_H,
                 lbuf, C_SURFACE, C_SUBTEXT, OnscreenAction::KB_CHAR, c);
    }

    // ── Letter rows 0-2 ──────────────────────────────────────────
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 10; col++) {
            int bx = col * KEY_W;
            int by = KB_Y + row * KEY_H;
            char uc = KB_KEYS[row][col];

            if (row == 2 && col == 9) {
                draw_key(bx, by, KEY_W, KEY_H, "<",
                         C_YELLOW, C_BG, OnscreenAction::KB_BACKSPACE);
            } else {
                char dc = _kbShift ? uc : (char)tolower(uc);
                char lbuf[3] = {dc, 0, 0};
                draw_key(bx, by, KEY_W, KEY_H, lbuf,
                         C_SURFACE2, C_TEXT, OnscreenAction::KB_CHAR, dc);
            }
        }
    }

    // ── Bottom row: CAPS(96) | SPACE(192) | OK(192) ──────────────
    uint16_t caps_bg = _kbCaps  ? C_ACCENT
                     : _kbShift ? C_SURFACE
                     : C_SURFACE2;
    uint16_t caps_fg = _kbCaps ? C_BG : C_TEXT;
    draw_key(0, BOT_Y, 96, KEY_H,
             _kbCaps ? "CAPS" : (_kbShift ? "ABC" : "abc"),
             caps_bg, caps_fg, OnscreenAction::KB_CAPS);

    draw_key(96, BOT_Y, 192, KEY_H,
             "SPACE", C_SURFACE2, C_TEXT, OnscreenAction::KB_CHAR, ' ');

    draw_key(288, BOT_Y, 192, KEY_H,
             "OK", C_GREEN, C_BG, OnscreenAction::KB_CONFIRM);

    _spr.drawFastHLine(0, BOT_Y + KEY_H, SCR_W, C_BORDER);
    commit();
}

// ─────────────────────────────────────────────────────────
//   Search keyboard  (no number row, taller keys, suggestion)
// ─────────────────────────────────────────────────────────

static String suggestion_suffix(const String &typed, const String &suggestion) {
    if (typed.isEmpty() || suggestion.isEmpty()) return "";
    String tl = typed; tl.toLowerCase();
    String sl = suggestion; sl.toLowerCase();
    int pos = sl.indexOf(tl);
    if (pos >= 0) return suggestion.substring(pos + typed.length());
    return ""; // no visual suffix if no substring match
}

void Display::showSearchEntry(const String &current, const String &suggestion) {
    if (!_initialized) return;

    _spr.fillSprite(C_BG);
    clear_regions();

    // ── Layout: 480×320 ─────────────────────────────────────────
    // Header     44 px  (y=0)
    // Input bar  36 px  (y=44)
    // Letter×3   60 px each  (y=80/140/200)
    // Bottom row 60 px  (y=260)
    static constexpr int KEY_W  = 48;   // 10 cols × 48 = 480
    static constexpr int KEY_H  = 60;
    static constexpr int INP_H  = 36;
    static constexpr int KB_Y   = HDR_H + INP_H;  // 80
    static constexpr int BOT_Y  = KB_Y + 3 * KEY_H; // 260

    // ── Header ───────────────────────────────────────────────────
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("SUCHE", 8, HDR_H / 2);

    // ── Input bar ────────────────────────────────────────────────
    _spr.fillRect(0, HDR_H, SCR_W, INP_H, C_SURFACE2);
    _spr.drawFastHLine(0, HDR_H + INP_H - 1, SCR_W, C_BORDER);
    // Tapping the bar accepts the suggestion
    add_region(0, HDR_H, SCR_W, INP_H, OnscreenAction::INV_SEARCH);

    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);

    if (current.isEmpty()) {
        _spr.setTextColor(C_SUBTEXT, C_SURFACE2);
        _spr.drawString("Suchen...", 8, HDR_H + INP_H / 2);
    } else {
        // Draw typed text + cursor
        String typed_cur = trunc(current, 38) + "|";
        _spr.setTextColor(C_TEXT, C_SURFACE2);
        _spr.drawString(typed_cur.c_str(), 8, HDR_H + INP_H / 2);

        // Draw suggestion suffix starting right after typed+cursor
        String suffix = suggestion_suffix(current, suggestion);
        if (!suffix.isEmpty()) {
            int measured = (int)_spr.textWidth(typed_cur.c_str(), 2);
            String suf_trunc = trunc(suffix, 22);
            _spr.setTextColor(C_SUBTEXT, C_SURFACE2);
            _spr.drawString(suf_trunc.c_str(), 8 + measured, HDR_H + INP_H / 2);
        }
    }

    // ── Helper lambda: draw one key ───────────────────────────────
    auto draw_key = [&](int bx, int by, int w, int h,
                        const char *label, uint16_t bg, uint16_t fg,
                        OnscreenAction act, char reg = 0) {
        _spr.fillRect(bx, by, w, h, bg);
        _spr.drawFastVLine(bx, by, h, C_BORDER);
        _spr.drawFastHLine(bx, by, w, C_BORDER);
        _spr.setTextColor(fg, bg);
        _spr.setTextFont(2);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(label, bx + w / 2, by + h / 2);
        add_region(bx, by, w, h, act, reg);
    };

    // ── Letter rows 0-2 ──────────────────────────────────────────
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 10; col++) {
            int bx = col * KEY_W;
            int by = KB_Y + row * KEY_H;
            char uc = KB_KEYS[row][col];

            if (row == 2 && col == 9) {
                // Backspace slot
                draw_key(bx, by, KEY_W, KEY_H, "<",
                         C_YELLOW, C_BG, OnscreenAction::KB_BACKSPACE);
            } else {
                char dc = _kbShift ? uc : (char)tolower(uc);
                char lbuf[3] = {dc, 0, 0};
                draw_key(bx, by, KEY_W, KEY_H, lbuf,
                         C_SURFACE2, C_TEXT, OnscreenAction::KB_CHAR, dc);
            }
        }
    }

    // ── Bottom row: CAPS(96) | SPACE(192) | OK(192) ──────────────
    uint16_t caps_bg = _kbCaps  ? C_ACCENT
                     : _kbShift ? C_SURFACE
                     : C_SURFACE2;
    uint16_t caps_fg = _kbCaps ? C_BG : C_TEXT;
    draw_key(0, BOT_Y, 96, KEY_H,
             _kbCaps ? "CAPS" : (_kbShift ? "ABC" : "abc"),
             caps_bg, caps_fg, OnscreenAction::KB_CAPS);

    draw_key(96, BOT_Y, 192, KEY_H,
             "SPACE", C_SURFACE2, C_TEXT, OnscreenAction::KB_CHAR, ' ');

    draw_key(288, BOT_Y, 192, KEY_H,
             "OK", C_GREEN, C_BG, OnscreenAction::KB_CONFIRM);

    _spr.drawFastHLine(0, BOT_Y + KEY_H, SCR_W, C_BORDER);
    commit();
}

// ─────────────────────────────────────────────────────────
//   Location selection
// ─────────────────────────────────────────────────────────

void Display::setActiveLocation(const String &loc) {
    _s_active_location = loc;
}

void Display::setActiveLocationColor(const String &hexColor) {
    _s_location_color = hexToRgb565(hexColor);
}

void Display::showLocationSelect(const String &current, const std::vector<String> &locations) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

    // Header
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString("LAGERORTE", 12, HDR_H / 2);
    if (!current.isEmpty()) {
        _spr.setTextColor(C_SUBTEXT, C_SURFACE);
        _spr.setTextFont(2);
        _spr.setTextDatum(MR_DATUM);
        _spr.drawString(("Aktiv: " + current).c_str(), SCR_W - 8, HDR_H / 2);
    }

    // Build list: "Kein Lagerort" first, then actual locations
    std::vector<String> rows;
    rows.push_back("-- Kein Lagerort --");
    for (const String &l : locations) rows.push_back(l);

    static constexpr int ITEM_H   = 55;
    static constexpr int MAX_SHOW = 5;
    int shown = (int)rows.size() < MAX_SHOW ? (int)rows.size() : MAX_SHOW;
    int list_y = HDR_H;

    for (int i = 0; i < shown; i++) {
        int iy = list_y + i * ITEM_H;
        bool isActive = (i == 0 && current.isEmpty()) ||
                        (i > 0 && rows[i] == current);
        uint16_t bg = isActive ? C_ACCENT : (i % 2 == 0 ? C_SURFACE : C_SURFACE2);
        uint16_t fg = isActive ? C_BG     : C_TEXT;
        _spr.fillRect(0, iy, SCR_W, ITEM_H, bg);
        _spr.fillRect(0, iy, 3, ITEM_H, isActive ? C_BG : C_ACCENT);
        _spr.setTextColor(fg, bg);
        _spr.setTextFont(4);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(trunc(rows[i], 36).c_str(), 12, iy + ITEM_H / 2);
        if (isActive) {
            _spr.setTextColor(C_BG, bg);
            _spr.setTextDatum(MR_DATUM);
            _spr.drawString("*", SCR_W - 12, iy + ITEM_H / 2);
        }
        _spr.drawFastHLine(0, iy + ITEM_H - 1, SCR_W, C_BORDER);
        add_region(0, iy, SCR_W, ITEM_H, LIST_ACTIONS[i]);
    }

    if (rows.size() == 1) {
        _spr.setTextColor(C_SUBTEXT, C_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString("Keine Lagerorte – im Web-UI anlegen", SCR_W / 2, HDR_H + 100);
    }

    commit();
}
