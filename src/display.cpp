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
static constexpr int MAX_REGIONS = 40;

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

// ─────────────────── home screen cache ───────────────────
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
    // WiFi indicator top-right
    uint16_t wc = wifiConnected ? C_GREEN : C_RED;
    _spr.fillCircle(SCR_W - 24, HDR_H / 2, 7, wc);
    _spr.drawCircle(SCR_W - 24, HDR_H / 2, 7, C_BORDER);
    _spr.setTextColor(C_SURFACE, wc);
    _spr.setTextFont(1);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("W", SCR_W - 24, HDR_H / 2);
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
    _spr.drawString("BALD ABLAUFEND", 250, scan_y + 6);
    _spr.setTextColor(C_YELLOW, C_SURFACE);
    _spr.drawString("Keine Eintraege ablaufend", 250, scan_y + 26);

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
    String ssid_str = s.wifiConnected ? trunc(s.ssid, 22) : "Setup-AP: " + String(AP_SSID);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.drawString(ssid_str.c_str(), 12, py + 66);
    String ip_str = s.ip.isEmpty() ? "192.168.4.1" : s.ip;
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.drawString(ip_str.c_str(), 12, py + 84);
    draw_button(12, py + 100, 210, 30,
                "Setup-AP starten", C_YELLOW, C_BG, 2, OnscreenAction::START_AP);
    draw_card(242, py + 8, 234, 120, C_SURFACE, C_ACCENT);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("GERAET", 250, py + 14);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.drawString("FoodScanner ESP32-S3", 250, py + 34);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.drawString("ST7796 | FT6336 | 480x320", 250, py + 54);

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
    _spr.drawString("Manuelle Eingabe", SCR_W / 2, py + 16);
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

                // Swipe: horizontal move > 80px and more horizontal than vertical
                bool is_swipe = (abs(dx) > 80) && (abs(dx) > abs(dy) * 2);

                OnscreenAction hit = OnscreenAction::NONE;
                char           hit_char = 0;

                if (is_swipe && py > HDR_H && py < SCR_H - TAB_H) {
                    hit = (dx < 0) ? OnscreenAction::SWIPE_LEFT : OnscreenAction::SWIPE_RIGHT;
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
//   WiFi / Dashboard delegates
// ─────────────────────────────────────────────────────────

void Display::showWiFiStatus(const String &ssid, const String &ip, bool connected) {
    showHome(UiTab::SYSTEM, ssid, ip, connected, "", "", "", "", 0,
             connected ? "WLAN verbunden" : "Setup-AP aktiv");
}

void Display::showDashboard(const String &ssid, const String &ip, bool wifiConnected,
                             const String &scannerStatus, const String &scannerName,
                             const String &lastScan, const String &lastType,
                             const String &message) {
    showHome(UiTab::STORE, ssid, ip, wifiConnected, scannerStatus, scannerName,
             lastScan, lastType, _homeState.inventoryCount, message);
}

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

    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_TEXT, C_SURFACE);
    _spr.setTextFont(2);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(trunc(product.name, 28).c_str(), 8, HDR_H / 2);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextDatum(MR_DATUM);
    _spr.drawString("MHD eingeben", SCR_W - 8, HDR_H / 2);

    _spr.fillRect(0, HDR_H, SCR_W, 72, C_SURFACE2);
    _spr.drawFastHLine(0, HDR_H + 72 - 1, SCR_W, C_BORDER);
    String d = dateDraft;
    while ((int)d.length() < 6) d += '_';
    String fmt = d.substring(0, 2) + "." + d.substring(2, 4) + "." + d.substring(4, 6);
    _spr.setTextColor(C_ACCENT, C_SURFACE2);
    _spr.setTextFont(6);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(fmt.c_str(), SCR_W / 2, HDR_H + 36);

    static const char *keys[12] = { "1","2","3","4","5","6","7","8","9","<","0","OK" };
    static const OnscreenAction key_actions[12] = {
        OnscreenAction::DATE_DIGIT_1, OnscreenAction::DATE_DIGIT_2, OnscreenAction::DATE_DIGIT_3,
        OnscreenAction::DATE_DIGIT_4, OnscreenAction::DATE_DIGIT_5, OnscreenAction::DATE_DIGIT_6,
        OnscreenAction::DATE_DIGIT_7, OnscreenAction::DATE_DIGIT_8, OnscreenAction::DATE_DIGIT_9,
        OnscreenAction::DATE_BACKSPACE, OnscreenAction::DATE_DIGIT_0, OnscreenAction::DATE_CONFIRM
    };
    int numpad_y = HDR_H + 72;
    int btn_w = 160, btn_h = 51;
    for (int i = 0; i < 12; i++) {
        int row = i / 3, col = i % 3;
        int bx = col * btn_w, by = numpad_y + row * btn_h;
        uint16_t bg = (i == 11) ? C_GREEN : (i == 9) ? C_YELLOW : C_SURFACE2;
        uint16_t fg = (i == 11 || i == 9) ? C_BG : C_TEXT;
        _spr.fillRect(bx, by, btn_w, btn_h, bg);
        _spr.drawFastHLine(bx, by, btn_w, C_BORDER);
        _spr.drawFastVLine(bx, by, btn_h, C_BORDER);
        _spr.setTextColor(fg, bg);
        _spr.setTextFont((i == 9) ? 4 : 6);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString((i == 9) ? "<--" : keys[i], bx + btn_w / 2, by + btn_h / 2);
        add_region(bx, by, btn_w, btn_h, key_actions[i]);
    }
    _spr.drawFastHLine(0, numpad_y + 4 * btn_h, SCR_W, C_BORDER);
    commit();
}

// ─────────────────────────────────────────────────────────
//   Quantity entry
// ─────────────────────────────────────────────────────────

// ── Quantity grid helper (shared by showQuantityEntry and showTemplateMHD) ──
// Draws a 4-column × 3-row grid of quantity buttons (1–12).
// Highlighted button = current quantity. Returns nothing.
static void draw_qty_grid(int top_y, int selected) {
    static const OnscreenAction QTY_ACTIONS[12] = {
        OnscreenAction::QTY_1,  OnscreenAction::QTY_2,  OnscreenAction::QTY_3,
        OnscreenAction::QTY_4,  OnscreenAction::QTY_5,  OnscreenAction::QTY_6,
        OnscreenAction::QTY_7,  OnscreenAction::QTY_8,  OnscreenAction::QTY_9,
        OnscreenAction::QTY_10, OnscreenAction::QTY_11, OnscreenAction::QTY_12,
    };
    const int COLS = 4, ROWS = 3;
    const int BTN_W = 108, BTN_H = 52, GAP = 6;
    const int grid_w = COLS * BTN_W + (COLS - 1) * GAP;
    const int x0 = (SCR_W - grid_w) / 2;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int n = r * COLS + c + 1;  // 1..12
            int bx = x0 + c * (BTN_W + GAP);
            int by = top_y + r * (BTN_H + GAP);
            bool sel = (n == selected);
            uint16_t bg = sel ? C_ACCENT : C_SURFACE2;
            uint16_t fg = sel ? C_BG     : C_TEXT;
            // Always font 4 — previously `rad` (8 or 6) was mistakenly
            // passed as the font, making the selected button use font-8 (giant).
            draw_button(bx, by, BTN_W, BTN_H, String(n).c_str(), bg, fg, 4, QTY_ACTIONS[n - 1]);
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

    // Subheader
    _spr.setTextColor(C_ACCENT, C_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("Menge ausw\xE4hlen", SCR_W / 2, HDR_H + 8);

    // 4×3 quantity grid
    draw_qty_grid(HDR_H + 28, quantity);

    // Bottom buttons
    int bot_y = SCR_H - 52;
    draw_button(4,            bot_y, 120, 44, "Zurueck",     C_RED,   C_TEXT, 2, OnscreenAction::CANCEL);
    draw_button(SCR_W - 174, bot_y, 170, 44, "Einlagern ->", C_GREEN, C_BG,   2, OnscreenAction::QTY_CONFIRM);
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

void Display::showInventoryList(const std::vector<InventoryItem> &items) {
    if (!_initialized) return;
    _spr.fillSprite(C_BG);
    clear_regions();

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

    _spr.fillRect(0, HDR_H, SCR_W, 28, C_SURFACE2);
    _spr.setTextColor(C_SUBTEXT, C_SURFACE2);
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("Produkt", 8,  HDR_H + 7);
    _spr.drawString("MHD",    310, HDR_H + 7);
    _spr.drawString("Menge",  430, HDR_H + 7);
    _spr.drawFastHLine(0, HDR_H + 28, SCR_W, C_BORDER);

    static constexpr int ROW_H = 28, MAX_ROWS = 8;
    int list_y = HDR_H + 28, shown = 0;
    int start = static_cast<int>(items.size()) - 1;
    for (int i = start; i >= 0 && shown < MAX_ROWS; i--, shown++) {
        const InventoryItem &item = items[i];
        int ry = list_y + shown * ROW_H;
        uint16_t row_bg = (shown % 2 == 0) ? C_SURFACE : C_SURFACE2;
        _spr.fillRect(0, ry, SCR_W, ROW_H, row_bg);
        _spr.setTextColor(C_TEXT, row_bg);
        _spr.setTextFont(2);
        _spr.setTextDatum(ML_DATUM);
        _spr.drawString(trunc(item.name, 22).c_str(), 8, ry + ROW_H / 2);
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
    _spr.drawString("Kategorie w\xE4hlen", 12, HDR_H / 2);

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

    if (showBack) {
        _spr.setTextColor(C_SUBTEXT, C_SURFACE);
        _spr.setTextFont(2);
        _spr.setTextDatum(MR_DATUM);
        _spr.drawString("< wischen = zur\xFCck", SCR_W - 8, HDR_H / 2);
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
    _spr.setTextColor(C_SUBTEXT, C_SURFACE);
    _spr.setTextDatum(MR_DATUM);
    _spr.drawString("< wischen = zur\xFCck", SCR_W - 8, HDR_H / 2);

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

static const char KB_KEYS[3][10] = {
    {'Q','W','E','R','T','Z','U','I','O','P'},
    {'A','S','D','F','G','H','J','K','L','.'},
    {'Y','X','C','V','B','N','M',',','-', 0 },  // 0 = backspace slot
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

    // Auto-uppercase when text is empty (start of input)
    if (current.isEmpty()) _kbShift = _kbCaps ? true : true;

    _spr.fillSprite(C_BG);
    clear_regions();

    // Header (44px)
    _spr.fillRect(0, 0, SCR_W, HDR_H, C_SURFACE);
    _spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_BORDER);
    _spr.setTextColor(C_ACCENT, C_SURFACE);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(title, 8, HDR_H / 2);

    // Input display (44px, y=44)
    int inp_y = HDR_H;
    _spr.fillRect(0, inp_y, SCR_W, 44, C_SURFACE2);
    _spr.drawFastHLine(0, inp_y + 43, SCR_W, C_BORDER);
    String disp = current.isEmpty() ? "_ _ _" : (trunc(current, 36) + "_");
    _spr.setTextColor(C_TEXT, C_SURFACE2);
    _spr.setTextFont(4);
    _spr.setTextDatum(ML_DATUM);
    _spr.drawString(disp.c_str(), 8, inp_y + 22);

    // Keyboard rows 0-2 (3 × 10 keys, 48×58 each, y=88)
    int kb_y = inp_y + 44;
    int key_w = 48, key_h = 58;

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 10; col++) {
            int bx = col * key_w;
            int by = kb_y + row * key_h;
            char upper_c = KB_KEYS[row][col];

            OnscreenAction act;
            uint16_t bg, fg;
            char lbuf[3] = {0};
            const char *label;

            if (row == 2 && col == 9) {
                act   = OnscreenAction::KB_BACKSPACE;
                label = "<";
                bg    = C_YELLOW; fg = C_BG;
            } else {
                char disp_c = _kbShift ? upper_c : (char)tolower(upper_c);
                lbuf[0] = disp_c;
                label   = lbuf;
                act     = OnscreenAction::KB_CHAR;
                bg      = C_SURFACE2; fg = C_TEXT;
            }

            _spr.fillRect(bx, by, key_w, key_h, bg);
            _spr.drawFastVLine(bx, by, key_h, C_BORDER);
            _spr.drawFastHLine(bx, by, key_w, C_BORDER);
            _spr.setTextColor(fg, bg);
            _spr.setTextFont(4);
            _spr.setTextDatum(MC_DATUM);
            _spr.drawString(label, bx + key_w / 2, by + key_h / 2);

            char reg_char = (act == OnscreenAction::KB_CHAR)
                ? (_kbShift ? upper_c : (char)tolower(upper_c))
                : 0;
            add_region(bx, by, key_w, key_h, act, reg_char);
        }
    }

    // Row 3: CAPS (96px) | SPACE (192px) | OK (192px)
    int row4_y = kb_y + 3 * key_h;

    // CAPS key: highlighted when shift active
    uint16_t caps_bg = _kbCaps  ? C_ACCENT
                     : _kbShift ? C_SURFACE  // temporary shift after space/start
                     : C_SURFACE2;
    uint16_t caps_fg = _kbCaps ? C_BG : C_TEXT;
    _spr.fillRect(0, row4_y, 96, key_h, caps_bg);
    _spr.drawFastVLine(0,  row4_y, key_h, C_BORDER);
    _spr.drawFastHLine(0,  row4_y, 96,   C_BORDER);
    _spr.setTextColor(caps_fg, caps_bg);
    _spr.setTextFont(2);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(_kbCaps ? "CAPS" : "abc", 48, row4_y + key_h / 2);
    add_region(0, row4_y, 96, key_h, OnscreenAction::KB_CAPS);

    // SPACE key
    _spr.fillRect(96, row4_y, 192, key_h, C_SURFACE2);
    _spr.drawFastVLine(96,  row4_y, key_h, C_BORDER);
    _spr.drawFastHLine(96,  row4_y, 192,  C_BORDER);
    _spr.setTextColor(C_TEXT, C_SURFACE2);
    _spr.setTextFont(4);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("SPACE", 96 + 96, row4_y + key_h / 2);
    add_region(96, row4_y, 192, key_h, OnscreenAction::KB_CHAR, ' ');

    // OK key
    _spr.fillRect(288, row4_y, 192, key_h, C_GREEN);
    _spr.drawFastVLine(288, row4_y, key_h, C_BORDER);
    _spr.drawFastHLine(288, row4_y, 192,  C_BORDER);
    _spr.setTextColor(C_BG, C_GREEN);
    _spr.setTextFont(4);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("OK", 288 + 96, row4_y + key_h / 2);
    add_region(288, row4_y, 192, key_h, OnscreenAction::KB_CONFIRM);

    _spr.drawFastHLine(0, row4_y + key_h, SCR_W, C_BORDER);
    commit();
}
