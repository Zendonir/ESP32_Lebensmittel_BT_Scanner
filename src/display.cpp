/**
 * display.cpp – LVGL 8.3 UI for FoodScanner (Waveshare ESP32-S3-Touch-LCD-3.5)
 * Display: ST7796 SPI, 480×320 landscape  |  Touch: FT6336 I2C
 *
 * Screen layout
 * ┌──────────────────────────────────────────────┐
 * │  Header bar                            44 px │
 * ├──────────────────────────────────────────────┤
 * │  Content area  (4 panels, one visible) 224 px│
 * ├──────────────────────────────────────────────┤
 * │  Bottom tab bar                        52 px │
 * └──────────────────────────────────────────────┘
 */

#include "display.h"
#include "touch.h"
#include "models/InventoryItem.h"
#include "models/ProductInfo.h"
#include "config.h"

#include <TFT_eSPI.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

// ─────────────────── layout constants ────────────────────
static constexpr int SCR_W      = DISPLAY_LANDSCAPE_WIDTH;   // 480
static constexpr int SCR_H      = DISPLAY_LANDSCAPE_HEIGHT;  // 320
static constexpr int HDR_H      = 44;
static constexpr int TAB_H      = 52;
static constexpr int CNT_Y      = HDR_H;
static constexpr int CNT_H      = SCR_H - HDR_H - TAB_H;    // 224
static constexpr int LV_BUF_LINES = 40;                      // draw-buffer stripe

// ─────────────────── colour palette ──────────────────────
#define C_BG        lv_color_hex(0x0D1117)
#define C_SURFACE   lv_color_hex(0x161B22)
#define C_SURFACE2  lv_color_hex(0x21262D)
#define C_BORDER    lv_color_hex(0x30363D)
#define C_TEXT      lv_color_hex(0xE6EDF3)
#define C_SUBTEXT   lv_color_hex(0x8B949E)
#define C_ACCENT    lv_color_hex(0x58A6FF)
#define C_GREEN     lv_color_hex(0x3FB950)
#define C_YELLOW    lv_color_hex(0xD29922)
#define C_RED       lv_color_hex(0xF85149)
#define C_ORANGE    lv_color_hex(0xDB6D28)
#define C_PURPLE    lv_color_hex(0xBC8CFF)

// ─────────────────── LVGL driver state ───────────────────
static TFT_eSPI        _tft;
static lv_disp_draw_buf_t _draw_buf;
static lv_color_t*     _lv_buf1 = nullptr;
static lv_color_t*     _lv_buf2 = nullptr;
static lv_disp_drv_t   _disp_drv;
static lv_indev_drv_t  _indev_drv;

// ─────────────────── screen / widget handles ─────────────
// Header
static lv_obj_t* _lbl_hdr_title;
static lv_obj_t* _lbl_hdr_wifi;

// Tab bar buttons + indicators
static lv_obj_t* _btn_tab[4];
static lv_obj_t* _lbl_tab[4];
static const char* _tab_labels[4] = { LV_SYMBOL_HOME,     LV_SYMBOL_LIST,
                                       LV_SYMBOL_EYE_OPEN, LV_SYMBOL_SETTINGS };
static const char* _tab_names[4]  = { "Home", "Inventar", "Scan", "System" };

// Content panels
static lv_obj_t* _panel[4];   // STORE=0, INVENTORY=1, SCANNER=2, SYSTEM=3

// Dashboard / STORE panel widgets
static lv_obj_t* _lbl_stat_total;
static lv_obj_t* _lbl_stat_expiring;
static lv_obj_t* _lbl_stat_critical;
static lv_obj_t* _lbl_stat_shopping;
static lv_obj_t* _pill_wifi;
static lv_obj_t* _pill_mqtt;
static lv_obj_t* _pill_scanner;
static lv_obj_t* _lbl_scan_code;
static lv_obj_t* _lbl_scan_type;
static lv_obj_t* _lbl_status_msg;
static lv_obj_t* _list_expiring;
static lv_obj_t* _list_activity;

// Inventar panel
static lv_obj_t* _list_inventar;
static lv_obj_t* _lbl_inv_count;

// Scanner panel
static lv_obj_t* _lbl_ble_status;
static lv_obj_t* _lbl_ble_name;
static lv_obj_t* _btn_ble_reconnect;

// System panel
static lv_obj_t* _lbl_sys_wifi;
static lv_obj_t* _lbl_sys_ip;
static lv_obj_t* _lbl_sys_ssid;
static lv_obj_t* _btn_start_ap;

// ──── Overlay / dialog panels ────
static lv_obj_t* _ovl_bg;          // semi-transparent backdrop
static lv_obj_t* _dlg_fetching;
static lv_obj_t* _lbl_fetch_code;
static lv_obj_t* _dlg_date;
static lv_obj_t* _lbl_date_product;
static lv_obj_t* _lbl_date_draft;
static lv_obj_t* _dlg_qty;
static lv_obj_t* _lbl_qty_product;
static lv_obj_t* _lbl_qty_value;
static lv_obj_t* _dlg_result;
static lv_obj_t* _lbl_result_title;
static lv_obj_t* _lbl_result_msg;
static lv_obj_t* _dlg_splash;

// ─────────────────── action queue (single-slot) ──────────
static volatile OnscreenAction _pending_action = OnscreenAction::NONE;

static void enqueue(OnscreenAction a) { _pending_action = a; }

// ═════════════════════════════════════════════════════════
//   LVGL driver callbacks
// ═════════════════════════════════════════════════════════

static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    _tft.startWrite();
    _tft.setAddrWindow(area->x1, area->y1, w, h);
    _tft.pushColors(reinterpret_cast<uint16_t*>(px), w * h, true);
    _tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t* /*drv*/, lv_indev_data_t* data) {
    TouchPoint p = touch_obj.read();
    if (p.pressed) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = p.x;
        data->point.y = p.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ═════════════════════════════════════════════════════════
//   Style helpers
// ═════════════════════════════════════════════════════════

static void style_card(lv_obj_t* obj, lv_color_t border_col = C_BORDER) {
    lv_obj_set_style_bg_color(obj, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, border_col, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_panel(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, C_BG, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

static lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                             const lv_font_t* font, lv_color_t col,
                             lv_align_t align = LV_ALIGN_DEFAULT,
                             int x = 0, int y = 0) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static lv_obj_t* make_btn(lv_obj_t* parent, const char* label,
                           lv_color_t bg, lv_color_t fg,
                           int w, int h, lv_event_cb_t cb,
                           lv_align_t align = LV_ALIGN_DEFAULT,
                           int x = 0, int y = 0) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, align, x, y);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(bg, LV_OPA_20), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, fg, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text, lv_color_t bg,
                            int x, int y) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_SIZE_CONTENT, 22);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_color(c, bg, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 11, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_hor(c, 8, 0);
    lv_obj_set_style_pad_ver(c, 3, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(c);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    return c;
}

// ═════════════════════════════════════════════════════════
//   Stat card builder (for the 4 top cards on Dashboard)
// ═════════════════════════════════════════════════════════

struct StatCard {
    lv_obj_t* count_lbl;
    lv_obj_t* sub_lbl;
};

static StatCard make_stat_card(lv_obj_t* parent, const char* title,
                                const char* count, lv_color_t accent,
                                int x, int y, int w = 110, int h = 64) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // left accent bar
    lv_obj_t* bar = lv_obj_create(card);
    lv_obj_set_size(bar, 4, h - 2);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, accent, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cnt = lv_label_create(card);
    lv_label_set_text(cnt, count);
    lv_obj_set_style_text_font(cnt, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(cnt, accent, 0);
    lv_obj_set_pos(cnt, 12, 8);

    lv_obj_t* ttl = lv_label_create(card);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ttl, C_SUBTEXT, 0);
    lv_obj_set_pos(ttl, 12, 38);

    return {cnt, ttl};
}

// ═════════════════════════════════════════════════════════
//   Panel builders
// ═════════════════════════════════════════════════════════

static void build_panel_store() {
    lv_obj_t* p = _panel[0];

    // ── Stat cards row (y=4) ──────────────────────────────
    auto sc0 = make_stat_card(p, "Produkte", "0",  C_ACCENT,  4,  4, 110, 64);
    auto sc1 = make_stat_card(p, "Ablaufend","0",  C_YELLOW, 118,  4, 110, 64);
    auto sc2 = make_stat_card(p, "Kritisch", "0",  C_RED,    232,  4, 110, 64);
    auto sc3 = make_stat_card(p, "Einkauf",  "0",  C_GREEN,  346,  4, 110, 64);
    _lbl_stat_total    = sc0.count_lbl;
    _lbl_stat_expiring = sc1.count_lbl;
    _lbl_stat_critical = sc2.count_lbl;
    _lbl_stat_shopping = sc3.count_lbl;

    // ── Status pills row (y=76) ───────────────────────────
    _pill_wifi    = make_pill(p, LV_SYMBOL_WIFI " WLAN",    C_SURFACE2, 4,  76);
    _pill_mqtt    = make_pill(p, "MQTT",                    C_SURFACE2, 90, 76);
    _pill_scanner = make_pill(p, LV_SYMBOL_BLUETOOTH " BLE", C_SURFACE2, 150, 76);

    // ── Last scan card (y=106) ───────────────────────────
    lv_obj_t* scan_card = lv_obj_create(p);
    lv_obj_set_pos(scan_card, 4, 106);
    lv_obj_set_size(scan_card, 234, 64);
    style_card(scan_card, C_ACCENT);

    lv_obj_t* sc_title = lv_label_create(scan_card);
    lv_label_set_text(sc_title, "LETZTER SCAN");
    lv_obj_set_style_text_font(sc_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sc_title, C_SUBTEXT, 0);
    lv_obj_set_pos(sc_title, 0, 0);

    _lbl_scan_code = lv_label_create(scan_card);
    lv_label_set_text(_lbl_scan_code, "Bereit zum Scannen");
    lv_obj_set_style_text_font(_lbl_scan_code, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_scan_code, C_TEXT, 0);
    lv_obj_set_pos(_lbl_scan_code, 0, 18);
    lv_label_set_long_mode(_lbl_scan_code, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_scan_code, 220);

    _lbl_scan_type = lv_label_create(scan_card);
    lv_label_set_text(_lbl_scan_type, "EAN scannen oder Label-QR");
    lv_obj_set_style_text_font(_lbl_scan_type, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_scan_type, C_SUBTEXT, 0);
    lv_obj_set_pos(_lbl_scan_type, 0, 38);
    lv_label_set_long_mode(_lbl_scan_type, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_scan_type, 220);

    // ── Bald ablaufend mini-list (y=106, x=242) ───────────
    lv_obj_t* exp_card = lv_obj_create(p);
    lv_obj_set_pos(exp_card, 242, 106);
    lv_obj_set_size(exp_card, 234, 64);
    style_card(exp_card, C_YELLOW);

    lv_obj_t* exp_title = lv_label_create(exp_card);
    lv_label_set_text(exp_title, "BALD ABLAUFEND");
    lv_obj_set_style_text_font(exp_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(exp_title, C_SUBTEXT, 0);
    lv_obj_set_pos(exp_title, 0, 0);

    _list_expiring = lv_obj_create(exp_card);
    lv_obj_set_pos(_list_expiring, 0, 18);
    lv_obj_set_size(_list_expiring, 220, 42);
    lv_obj_set_style_bg_opa(_list_expiring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_list_expiring, 0, 0);
    lv_obj_set_style_pad_all(_list_expiring, 0, 0);

    // ── Letzte Aktivität  (y=178) ─────────────────────────
    lv_obj_t* act_card = lv_obj_create(p);
    lv_obj_set_pos(act_card, 4, 178);
    lv_obj_set_size(act_card, 472, 44);
    style_card(act_card, C_ACCENT);

    lv_obj_t* act_title = lv_label_create(act_card);
    lv_label_set_text(act_title, "LETZTE AKTIVITÄT");
    lv_obj_set_style_text_font(act_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(act_title, C_SUBTEXT, 0);
    lv_obj_set_pos(act_title, 0, 0);

    _lbl_status_msg = lv_label_create(act_card);
    lv_label_set_text(_lbl_status_msg, "System bereit");
    lv_obj_set_style_text_font(_lbl_status_msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status_msg, C_TEXT, 0);
    lv_obj_set_pos(_lbl_status_msg, 0, 18);
    lv_label_set_long_mode(_lbl_status_msg, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_status_msg, 460);

    (void)_list_activity; // unused in this panel; activity is shown in _lbl_status_msg
}

static void build_panel_inventar() {
    lv_obj_t* p = _panel[1];

    lv_obj_t* hdr_row = lv_obj_create(p);
    lv_obj_set_pos(hdr_row, 4, 4);
    lv_obj_set_size(hdr_row, 472, 28);
    lv_obj_set_style_bg_color(hdr_row, C_SURFACE2, 0);
    lv_obj_set_style_bg_opa(hdr_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr_row, 0, 0);
    lv_obj_set_style_radius(hdr_row, 6, 0);
    lv_obj_set_style_pad_all(hdr_row, 6, 0);
    lv_obj_clear_flag(hdr_row, LV_OBJ_FLAG_SCROLLABLE);

    // Column headers
    static const char* col_hdrs[] = {"Produkt", "Marke", "MHD", "Tage", "Menge"};
    static const int   col_x[]    = {0, 160, 280, 370, 430};
    for (int i = 0; i < 5; i++) {
        lv_obj_t* lbl = lv_label_create(hdr_row);
        lv_label_set_text(lbl, col_hdrs[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, C_SUBTEXT, 0);
        lv_obj_set_pos(lbl, col_x[i], 0);
    }

    _lbl_inv_count = lv_label_create(p);
    lv_label_set_text(_lbl_inv_count, "0 Einträge");
    lv_obj_set_style_text_font(_lbl_inv_count, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_inv_count, C_SUBTEXT, 0);
    lv_obj_set_pos(_lbl_inv_count, 4, 196);

    _list_inventar = lv_obj_create(p);
    lv_obj_set_pos(_list_inventar, 4, 36);
    lv_obj_set_size(_list_inventar, 472, 156);
    lv_obj_set_style_bg_color(_list_inventar, C_BG, 0);
    lv_obj_set_style_bg_opa(_list_inventar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_list_inventar, 0, 0);
    lv_obj_set_style_pad_all(_list_inventar, 0, 0);
    lv_obj_set_style_pad_row(_list_inventar, 2, 0);
    lv_obj_set_flex_flow(_list_inventar, LV_FLEX_FLOW_COLUMN);
}

static void build_panel_scanner() {
    lv_obj_t* p = _panel[2];

    lv_obj_t* card = lv_obj_create(p);
    lv_obj_set_pos(card, 4, 8);
    lv_obj_set_size(card, 472, 100);
    style_card(card, C_YELLOW);

    lv_obj_t* ttl = lv_label_create(card);
    lv_label_set_text(ttl, "BLE-HID BARCODE SCANNER");
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ttl, C_SUBTEXT, 0);
    lv_obj_set_pos(ttl, 0, 0);

    _lbl_ble_status = lv_label_create(card);
    lv_label_set_text(_lbl_ble_status, "getrennt");
    lv_obj_set_style_text_font(_lbl_ble_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_ble_status, C_YELLOW, 0);
    lv_obj_set_pos(_lbl_ble_status, 0, 22);

    _lbl_ble_name = lv_label_create(card);
    lv_label_set_text(_lbl_ble_name, "Koppeln über Web-UI");
    lv_obj_set_style_text_font(_lbl_ble_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_ble_name, C_SUBTEXT, 0);
    lv_obj_set_pos(_lbl_ble_name, 0, 52);
    lv_label_set_long_mode(_lbl_ble_name, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_ble_name, 300);

    _btn_ble_reconnect = make_btn(card, "Verbinden / Trennen",
                                  C_ACCENT, lv_color_white(),
                                  200, 36,
                                  [](lv_event_t*){ enqueue(OnscreenAction::SCANNER_RECONNECT); },
                                  LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void build_panel_system() {
    lv_obj_t* p = _panel[3];

    lv_obj_t* wlan_card = lv_obj_create(p);
    lv_obj_set_pos(wlan_card, 4, 8);
    lv_obj_set_size(wlan_card, 230, 120);
    style_card(wlan_card, C_GREEN);

    lv_obj_t* wt = lv_label_create(wlan_card);
    lv_label_set_text(wt, LV_SYMBOL_WIFI "  NETZWERK");
    lv_obj_set_style_text_font(wt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wt, C_SUBTEXT, 0);
    lv_obj_set_pos(wt, 0, 0);

    _lbl_sys_wifi = lv_label_create(wlan_card);
    lv_label_set_text(_lbl_sys_wifi, "Verbinden...");
    lv_obj_set_style_text_font(_lbl_sys_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_sys_wifi, C_GREEN, 0);
    lv_obj_set_pos(_lbl_sys_wifi, 0, 20);

    _lbl_sys_ssid = lv_label_create(wlan_card);
    lv_label_set_text(_lbl_sys_ssid, "---");
    lv_obj_set_style_text_font(_lbl_sys_ssid, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_sys_ssid, C_TEXT, 0);
    lv_obj_set_pos(_lbl_sys_ssid, 0, 44);
    lv_label_set_long_mode(_lbl_sys_ssid, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_sys_ssid, 210);

    _lbl_sys_ip = lv_label_create(wlan_card);
    lv_label_set_text(_lbl_sys_ip, "---");
    lv_obj_set_style_text_font(_lbl_sys_ip, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_sys_ip, C_SUBTEXT, 0);
    lv_obj_set_pos(_lbl_sys_ip, 0, 68);

    _btn_start_ap = make_btn(wlan_card, "Setup-AP starten",
                             C_YELLOW, C_BG, 210, 32,
                             [](lv_event_t*){ enqueue(OnscreenAction::START_AP); },
                             LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* info_card = lv_obj_create(p);
    lv_obj_set_pos(info_card, 242, 8);
    lv_obj_set_size(info_card, 234, 120);
    style_card(info_card, C_ACCENT);

    lv_obj_t* it = lv_label_create(info_card);
    lv_label_set_text(it, "GERÄT");
    lv_obj_set_style_text_font(it, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(it, C_SUBTEXT, 0);
    lv_obj_set_pos(it, 0, 0);

    const char* dev_lines[] = {"FoodScanner ESP32-S3", "ST7796 | FT6336 | 480×320"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t* l = lv_label_create(info_card);
        lv_label_set_text(l, dev_lines[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, i == 0 ? C_TEXT : C_SUBTEXT, 0);
        lv_obj_set_pos(l, 0, 20 + i * 20);
    }

    make_btn(p, LV_SYMBOL_REFRESH " Refresh",
             C_ACCENT, lv_color_white(), 150, 36,
             [](lv_event_t*){ enqueue(OnscreenAction::REFRESH); },
             LV_ALIGN_BOTTOM_LEFT, 4, -4);
}

// ═════════════════════════════════════════════════════════
//   Overlay dialogs
// ═════════════════════════════════════════════════════════

static void hide_overlay();

static lv_obj_t* make_dialog(int w, int h) {
    lv_obj_t* d = lv_obj_create(_ovl_bg);
    lv_obj_set_size(d, w, h);
    lv_obj_center(d);
    lv_obj_set_style_bg_color(d, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(d, C_BORDER, 0);
    lv_obj_set_style_border_width(d, 1, 0);
    lv_obj_set_style_radius(d, 12, 0);
    lv_obj_set_style_pad_all(d, 16, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    return d;
}

static void build_overlay_fetching() {
    _dlg_fetching = make_dialog(380, 120);

    lv_obj_t* t = lv_label_create(_dlg_fetching);
    lv_label_set_text(t, LV_SYMBOL_DOWNLOAD " Open Food Facts – Suche läuft…");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, C_ACCENT, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    _lbl_fetch_code = lv_label_create(_dlg_fetching);
    lv_label_set_text(_lbl_fetch_code, "");
    lv_obj_set_style_text_font(_lbl_fetch_code, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_fetch_code, C_TEXT, 0);
    lv_obj_align(_lbl_fetch_code, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* spin = lv_spinner_create(_dlg_fetching, 1000, 60);
    lv_obj_set_size(spin, 32, 32);
    lv_obj_align(spin, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_arc_color(spin, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spin, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, C_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 3, LV_PART_MAIN);

    make_btn(_dlg_fetching, "Abbrechen", C_RED, lv_color_white(), 120, 34,
             [](lv_event_t*){ enqueue(OnscreenAction::CANCEL); hide_overlay(); },
             LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void build_overlay_dateentry() {
    _dlg_date = make_dialog(440, 200);

    lv_obj_t* ttl = lv_label_create(_dlg_date);
    lv_label_set_text(ttl, "MHD eingeben  (JJJJMMTT)");
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ttl, C_ACCENT, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 0);

    _lbl_date_product = lv_label_create(_dlg_date);
    lv_label_set_text(_lbl_date_product, "");
    lv_obj_set_style_text_font(_lbl_date_product, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_date_product, C_TEXT, 0);
    lv_obj_align(_lbl_date_product, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_label_set_long_mode(_lbl_date_product, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_date_product, 200);

    // Date draft display
    lv_obj_t* draft_box = lv_obj_create(_dlg_date);
    lv_obj_set_pos(draft_box, 0, 52);
    lv_obj_set_size(draft_box, 200, 40);
    lv_obj_set_style_bg_color(draft_box, C_SURFACE2, 0);
    lv_obj_set_style_bg_opa(draft_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(draft_box, C_ACCENT, 0);
    lv_obj_set_style_border_width(draft_box, 1, 0);
    lv_obj_set_style_radius(draft_box, 6, 0);
    lv_obj_set_style_pad_all(draft_box, 8, 0);
    lv_obj_clear_flag(draft_box, LV_OBJ_FLAG_SCROLLABLE);

    _lbl_date_draft = lv_label_create(draft_box);
    lv_label_set_text(_lbl_date_draft, "________");
    lv_obj_set_style_text_font(_lbl_date_draft, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_date_draft, C_ACCENT, 0);
    lv_obj_center(_lbl_date_draft);

    // Numpad  3×4  (210 wide, starts at x=220)
    static const char* keys[12] = {"1","2","3","4","5","6","7","8","9",LV_SYMBOL_BACKSPACE,"0","OK"};
    static const OnscreenAction key_actions[12] = {
        OnscreenAction::DATE_DIGIT_1, OnscreenAction::DATE_DIGIT_2, OnscreenAction::DATE_DIGIT_3,
        OnscreenAction::DATE_DIGIT_4, OnscreenAction::DATE_DIGIT_5, OnscreenAction::DATE_DIGIT_6,
        OnscreenAction::DATE_DIGIT_7, OnscreenAction::DATE_DIGIT_8, OnscreenAction::DATE_DIGIT_9,
        OnscreenAction::DATE_BACKSPACE, OnscreenAction::DATE_DIGIT_0, OnscreenAction::DATE_CONFIRM
    };

    for (int i = 0; i < 12; i++) {
        int row = i / 3, col = i % 3;
        lv_color_t bg = (i == 11) ? C_GREEN : (i == 9) ? C_YELLOW : C_SURFACE2;
        lv_color_t fg = (i == 11 || i == 9) ? C_BG : C_TEXT;

        lv_obj_t* b = lv_btn_create(_dlg_date);
        lv_obj_set_size(b, 60, 34);
        lv_obj_set_pos(b, 220 + col * 66, 20 + row * 40);
        lv_obj_set_style_bg_color(b, bg, 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);

        lv_obj_t* lbl = lv_label_create(b);
        lv_label_set_text(lbl, keys[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, fg, 0);
        lv_obj_center(lbl);

        OnscreenAction act = key_actions[i];
        lv_obj_add_event_cb(b, [](lv_event_t* e){
            enqueue(*static_cast<OnscreenAction*>(lv_event_get_user_data(e)));
        }, LV_EVENT_CLICKED, new OnscreenAction(act));
    }

    make_btn(_dlg_date, "Abbrechen", C_RED, lv_color_white(), 130, 34,
             [](lv_event_t*){ enqueue(OnscreenAction::CANCEL); },
             LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void build_overlay_qtyentry() {
    _dlg_qty = make_dialog(400, 160);

    lv_obj_t* ttl = lv_label_create(_dlg_qty);
    lv_label_set_text(ttl, "Menge bestätigen");
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ttl, C_ACCENT, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 0);

    _lbl_qty_product = lv_label_create(_dlg_qty);
    lv_label_set_text(_lbl_qty_product, "");
    lv_obj_set_style_text_font(_lbl_qty_product, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_qty_product, C_TEXT, 0);
    lv_obj_align(_lbl_qty_product, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_label_set_long_mode(_lbl_qty_product, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_qty_product, 368);

    make_btn(_dlg_qty, LV_SYMBOL_MINUS, C_YELLOW, C_BG, 50, 44,
             [](lv_event_t*){ enqueue(OnscreenAction::QTY_MINUS); },
             LV_ALIGN_CENTER, -80, 0);

    _lbl_qty_value = lv_label_create(_dlg_qty);
    lv_label_set_text(_lbl_qty_value, "1");
    lv_obj_set_style_text_font(_lbl_qty_value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_lbl_qty_value, C_TEXT, 0);
    lv_obj_align(_lbl_qty_value, LV_ALIGN_CENTER, 0, 0);

    make_btn(_dlg_qty, LV_SYMBOL_PLUS, C_GREEN, C_BG, 50, 44,
             [](lv_event_t*){ enqueue(OnscreenAction::QTY_PLUS); },
             LV_ALIGN_CENTER, 80, 0);

    make_btn(_dlg_qty, "Einlagern " LV_SYMBOL_OK, C_GREEN, C_BG, 160, 36,
             [](lv_event_t*){ enqueue(OnscreenAction::QTY_CONFIRM); },
             LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    make_btn(_dlg_qty, "Zurück", C_RED, lv_color_white(), 120, 36,
             [](lv_event_t*){ enqueue(OnscreenAction::CANCEL); },
             LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void build_overlay_result() {
    _dlg_result = make_dialog(380, 130);

    _lbl_result_title = lv_label_create(_dlg_result);
    lv_label_set_text(_lbl_result_title, "");
    lv_obj_set_style_text_font(_lbl_result_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_result_title, C_GREEN, 0);
    lv_obj_align(_lbl_result_title, LV_ALIGN_TOP_MID, 0, 0);

    _lbl_result_msg = lv_label_create(_dlg_result);
    lv_label_set_text(_lbl_result_msg, "");
    lv_obj_set_style_text_font(_lbl_result_msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_result_msg, C_TEXT, 0);
    lv_obj_align(_lbl_result_msg, LV_ALIGN_CENTER, 0, 4);
    lv_label_set_long_mode(_lbl_result_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lbl_result_msg, 348);

    make_btn(_dlg_result, "OK", C_ACCENT, lv_color_white(), 120, 36,
             [](lv_event_t*){ enqueue(OnscreenAction::REFRESH); hide_overlay(); },
             LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void build_overlay_splash() {
    _dlg_splash = lv_obj_create(_ovl_bg);
    lv_obj_set_size(_dlg_splash, SCR_W, SCR_H);
    lv_obj_set_pos(_dlg_splash, 0, 0);
    lv_obj_set_style_bg_color(_dlg_splash, C_BG, 0);
    lv_obj_set_style_bg_opa(_dlg_splash, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_dlg_splash, 0, 0);
    lv_obj_set_style_radius(_dlg_splash, 0, 0);
    lv_obj_set_style_pad_all(_dlg_splash, 0, 0);
    lv_obj_clear_flag(_dlg_splash, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* logo_box = lv_obj_create(_dlg_splash);
    lv_obj_set_size(logo_box, 360, 160);
    lv_obj_center(logo_box);
    lv_obj_set_style_bg_color(logo_box, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(logo_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(logo_box, C_ACCENT, 0);
    lv_obj_set_style_border_width(logo_box, 1, 0);
    lv_obj_set_style_radius(logo_box, 16, 0);
    lv_obj_set_style_pad_all(logo_box, 24, 0);
    lv_obj_clear_flag(logo_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* app_name = lv_label_create(logo_box);
    lv_label_set_text(app_name, "FoodScanner");
    lv_obj_set_style_text_font(app_name, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(app_name, C_TEXT, 0);
    lv_obj_align(app_name, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* sub = lv_label_create(logo_box);
    lv_label_set_text(sub, "Lebensmittel smart verwalten");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, C_ACCENT, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t* boot = lv_label_create(_dlg_splash);
    lv_label_set_text(boot, "System startet…");
    lv_obj_set_style_text_font(boot, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(boot, C_SUBTEXT, 0);
    lv_obj_align(boot, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_t* spin = lv_spinner_create(_dlg_splash, 1000, 60);
    lv_obj_set_size(spin, 28, 28);
    lv_obj_align(spin, LV_ALIGN_BOTTOM_MID, 0, -46);
    lv_obj_set_style_arc_color(spin, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spin, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, C_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 3, LV_PART_MAIN);
}

// ─────────────────────────────────────────────────────────
static void hide_overlay() {
    lv_obj_add_flag(_ovl_bg, LV_OBJ_FLAG_HIDDEN);
}

static void show_overlay(lv_obj_t* dlg) {
    // Hide all child dialogs, then show the requested one
    lv_obj_t* children[] = {
        _dlg_fetching, _dlg_date, _dlg_qty,
        _dlg_result,   _dlg_splash
    };
    for (auto* c : children) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_ovl_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_ovl_bg);
}

// ═════════════════════════════════════════════════════════
//   Header and tab bar
// ═════════════════════════════════════════════════════════

static void show_tab(UiTab tab) {
    for (int i = 0; i < 4; i++) {
        bool active = (i == static_cast<int>(tab));
        lv_obj_set_style_bg_color(_btn_tab[i],
            active ? C_ACCENT : C_SURFACE2, 0);
        lv_obj_set_style_text_color(_lbl_tab[i],
            active ? C_BG : C_SUBTEXT, 0);
        if (active)
            lv_obj_clear_flag(_panel[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(_panel[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_header(lv_obj_t* screen) {
    lv_obj_t* hdr = lv_obj_create(screen);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, SCR_W, HDR_H);
    lv_obj_set_style_bg_color(hdr, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Bottom separator line
    lv_obj_t* sep = lv_obj_create(hdr);
    lv_obj_set_pos(sep, 0, HDR_H - 1);
    lv_obj_set_size(sep, SCR_W, 1);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    _lbl_hdr_title = lv_label_create(hdr);
    lv_label_set_text(_lbl_hdr_title, LV_SYMBOL_CHARGE " FoodScanner");
    lv_obj_set_style_text_font(_lbl_hdr_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_hdr_title, C_ACCENT, 0);
    lv_obj_align(_lbl_hdr_title, LV_ALIGN_LEFT_MID, 12, 0);

    _lbl_hdr_wifi = lv_label_create(hdr);
    lv_label_set_text(_lbl_hdr_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(_lbl_hdr_wifi, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_hdr_wifi, C_SUBTEXT, 0);
    lv_obj_align(_lbl_hdr_wifi, LV_ALIGN_RIGHT_MID, -12, 0);
}

static void build_tabbar(lv_obj_t* screen) {
    lv_obj_t* bar = lv_obj_create(screen);
    lv_obj_set_pos(bar, 0, SCR_H - TAB_H);
    lv_obj_set_size(bar, SCR_W, TAB_H);
    lv_obj_set_style_bg_color(bar, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // Top separator
    lv_obj_t* sep = lv_obj_create(bar);
    lv_obj_set_pos(sep, 0, 0);
    lv_obj_set_size(sep, SCR_W, 1);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    static const OnscreenAction tab_actions[4] = {
        OnscreenAction::TAB_STORE, OnscreenAction::TAB_INVENTORY,
        OnscreenAction::TAB_SCANNER, OnscreenAction::TAB_SYSTEM
    };

    int btn_w = SCR_W / 4;
    for (int i = 0; i < 4; i++) {
        _btn_tab[i] = lv_btn_create(bar);
        lv_obj_set_pos(_btn_tab[i], i * btn_w, 1);
        lv_obj_set_size(_btn_tab[i], btn_w, TAB_H - 1);
        lv_obj_set_style_bg_color(_btn_tab[i], (i == 0) ? C_ACCENT : C_SURFACE2, 0);
        lv_obj_set_style_radius(_btn_tab[i], 0, 0);
        lv_obj_set_style_shadow_width(_btn_tab[i], 0, 0);
        lv_obj_set_style_border_width(_btn_tab[i], 0, 0);

        lv_obj_t* col = lv_obj_create(_btn_tab[i]);
        lv_obj_center(col);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* icon_lbl = lv_label_create(col);
        lv_label_set_text(icon_lbl, _tab_labels[i]);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(icon_lbl, (i == 0) ? C_BG : C_SUBTEXT, 0);

        lv_obj_t* name_lbl = lv_label_create(col);
        lv_label_set_text(name_lbl, _tab_names[i]);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name_lbl, (i == 0) ? C_BG : C_SUBTEXT, 0);

        _lbl_tab[i] = icon_lbl;  // keep ref for colour updates

        OnscreenAction act = tab_actions[i];
        lv_obj_add_event_cb(_btn_tab[i], [](lv_event_t* e){
            enqueue(*static_cast<OnscreenAction*>(lv_event_get_user_data(e)));
        }, LV_EVENT_CLICKED, new OnscreenAction(act));
    }
}

// ═════════════════════════════════════════════════════════
//   Full screen builder
// ═════════════════════════════════════════════════════════

static void build_ui() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);
    build_tabbar(scr);

    // 4 content panels (all positioned in the content area)
    for (int i = 0; i < 4; i++) {
        _panel[i] = lv_obj_create(scr);
        lv_obj_set_pos(_panel[i], 0, CNT_Y);
        lv_obj_set_size(_panel[i], SCR_W, CNT_H);
        style_panel(_panel[i]);
        lv_obj_add_flag(_panel[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_panel_store();
    build_panel_inventar();
    build_panel_scanner();
    build_panel_system();

    // Overlay backdrop (full screen, semi-transparent, initially hidden)
    _ovl_bg = lv_obj_create(scr);
    lv_obj_set_pos(_ovl_bg, 0, 0);
    lv_obj_set_size(_ovl_bg, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(_ovl_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_ovl_bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_ovl_bg, 0, 0);
    lv_obj_set_style_radius(_ovl_bg, 0, 0);
    lv_obj_set_style_pad_all(_ovl_bg, 0, 0);
    lv_obj_clear_flag(_ovl_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_ovl_bg, LV_OBJ_FLAG_HIDDEN);

    build_overlay_fetching();
    build_overlay_dateentry();
    build_overlay_qtyentry();
    build_overlay_result();
    build_overlay_splash();

    // Show panel 0 (STORE) by default
    show_tab(UiTab::STORE);
}

// ═════════════════════════════════════════════════════════
//   Display class implementation
// ═════════════════════════════════════════════════════════

Display display_obj;

Display::Display() {}
Display::~Display() {}

void Display::init() {
    if (_initialized) return;

    // ── TFT init ─────────────────────────────────────────
    _tft.init();
    _tft.setRotation(1);   // landscape
    _tft.fillScreen(TFT_BLACK);

    // ── LVGL init ────────────────────────────────────────
    lv_init();

    // Draw buffers must be in DMA-accessible internal SRAM, not SPIRAM.
    // MALLOC_CAP_DMA implies internal RAM on ESP32; SPI DMA cannot source PSRAM.
    size_t buf_bytes = SCR_W * LV_BUF_LINES * sizeof(lv_color_t);
    _lv_buf1 = static_cast<lv_color_t*>(
        heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    _lv_buf2 = static_cast<lv_color_t*>(
        heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!_lv_buf1 || !_lv_buf2) {
        // Fallback: smaller single buffer from any 8-bit-accessible memory
        if (_lv_buf1) { heap_caps_free(_lv_buf1); }
        if (_lv_buf2) { heap_caps_free(_lv_buf2); }
        size_t small = SCR_W * 10 * sizeof(lv_color_t);
        _lv_buf1 = static_cast<lv_color_t*>(heap_caps_malloc(small, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        _lv_buf2 = nullptr;
        lv_disp_draw_buf_init(&_draw_buf, _lv_buf1, _lv_buf2, SCR_W * 10);
    } else {
        lv_disp_draw_buf_init(&_draw_buf, _lv_buf1, _lv_buf2, SCR_W * LV_BUF_LINES);
    }

    lv_disp_drv_init(&_disp_drv);
    _disp_drv.hor_res   = SCR_W;
    _disp_drv.ver_res   = SCR_H;
    _disp_drv.flush_cb  = disp_flush_cb;
    _disp_drv.draw_buf  = &_draw_buf;
    lv_disp_drv_register(&_disp_drv);

    lv_indev_drv_init(&_indev_drv);
    _indev_drv.type             = LV_INDEV_TYPE_POINTER;
    _indev_drv.read_cb          = touch_read_cb;
    // Raise scroll threshold so short finger drift doesn't prevent a click.
    // Default is 10 px; 20 px is more forgiving on capacitive glass.
    _indev_drv.scroll_limit     = 20;
    // Allow 600 ms before a press becomes a long-press (default 400 ms).
    _indev_drv.long_press_time  = 600;
    lv_indev_drv_register(&_indev_drv);

    build_ui();

    _initialized = true;
    Serial.printf("[Display] LVGL UI ready (%d×%d)\n", SCR_W, SCR_H);
}

// ── tick: call every loop() iteration ────────────────────
// LV_TICK_CUSTOM=1 in lv_conf.h wires millis() into LVGL's tick source,
// so lv_tick_inc() is not called here – lv_timer_handler() is sufficient.
void Display::tick() {
    if (!_initialized) return;
    lv_timer_handler();
}

// ── Legacy stubs ─────────────────────────────────────────
void Display::fillScreen(uint16_t color) {
    if (!_initialized) _tft.fillScreen(color);
}
void Display::drawText(int x, int y, const char* text, uint16_t /*color*/) {
    (void)x; (void)y; (void)text;
}
void Display::clear() { }

// ── hitTest: returns last LVGL action and clears it ───────
OnscreenAction Display::hitTest(uint16_t /*x*/, uint16_t /*y*/) const {
    OnscreenAction a = _pending_action;
    _pending_action = OnscreenAction::NONE;
    return a;
}

// ═════════════════════════════════════════════════════════
//   Screen transition methods
// ═════════════════════════════════════════════════════════

void Display::showSplash() {
    if (!_initialized) return;
    show_overlay(_dlg_splash);
    tick();
}

void Display::showWiFiStatus(const String &ssid, const String &ip, bool connected) {
    showHome(UiTab::SYSTEM, ssid, ip, connected, "", "", "", "", 0,
             connected ? "WLAN verbunden" : "Setup-AP aktiv");
}

void Display::showDashboard(const String &ssid, const String &ip, bool wifiConnected,
                             const String &scannerStatus, const String &scannerName,
                             const String &lastScan, const String &lastType,
                             const String &message) {
    showHome(UiTab::STORE, ssid, ip, wifiConnected, scannerStatus, scannerName,
             lastScan, lastType, 0, message);
}

void Display::showHome(UiTab activeTab,
                       const String &ssid, const String &ip, bool wifiConnected,
                       const String &scannerStatus, const String &scannerName,
                       const String &lastScan, const String &lastType,
                       size_t inventoryCount, const String &message) {
    if (!_initialized) return;
    hide_overlay();

    // ── Header wifi icon colour ───────────────────────────
    lv_obj_set_style_text_color(_lbl_hdr_wifi,
        wifiConnected ? C_GREEN : C_RED, 0);

    // ── Dashboard / STORE panel ──────────────────────────
    {
        lv_label_set_text(_lbl_stat_total, String(inventoryCount).c_str());

        // Scan info
        if (lastScan.isEmpty()) {
            lv_label_set_text(_lbl_scan_code, "Bereit zum Scannen");
            lv_label_set_text(_lbl_scan_type, "EAN scannen oder Label-QR");
        } else {
            lv_label_set_text(_lbl_scan_code, lastScan.c_str());
            lv_label_set_text(_lbl_scan_type, lastType.c_str());
        }

        // Status pills
        lv_obj_set_style_bg_color(_pill_wifi,
            wifiConnected ? C_GREEN : C_RED, 0);
        lv_obj_set_style_bg_color(_pill_scanner,
            (scannerStatus == "connected") ? C_GREEN : C_SURFACE2, 0);

        if (!message.isEmpty())
            lv_label_set_text(_lbl_status_msg, message.c_str());
    }

    // ── Scanner panel ─────────────────────────────────────
    {
        bool ble_ok = (scannerStatus == "connected");
        lv_label_set_text(_lbl_ble_status, ble_ok ? "Verbunden" : "Getrennt");
        lv_obj_set_style_text_color(_lbl_ble_status, ble_ok ? C_GREEN : C_YELLOW, 0);
        lv_label_set_text(_lbl_ble_name, scannerName.c_str());
    }

    // ── System panel ─────────────────────────────────────
    {
        lv_label_set_text(_lbl_sys_wifi, wifiConnected ? "Verbunden" : "Nicht verbunden");
        lv_obj_set_style_text_color(_lbl_sys_wifi, wifiConnected ? C_GREEN : C_RED, 0);
        String ssid_str = wifiConnected ? ssid : "Setup-AP: " + String(AP_SSID);
        lv_label_set_text(_lbl_sys_ssid, ssid_str.c_str());
        lv_label_set_text(_lbl_sys_ip,   ip.isEmpty() ? "192.168.4.1" : ip.c_str());
    }

    show_tab(activeTab);
}

void Display::showFetchingProduct(const String &barcode) {
    if (!_initialized) return;
    lv_label_set_text(_lbl_fetch_code, barcode.c_str());
    show_overlay(_dlg_fetching);
}

void Display::showDateEntry(const ProductInfo &product, const String &dateDraft) {
    if (!_initialized) return;
    lv_label_set_text(_lbl_date_product, product.name.c_str());

    // Format draft with placeholder underscores
    String display_draft = dateDraft;
    while (display_draft.length() < 8) display_draft += '_';
    // Insert separators: JJJJ-MM-TT
    String formatted = display_draft.substring(0, 4) + "-"
                     + display_draft.substring(4, 6) + "-"
                     + display_draft.substring(6, 8);
    lv_label_set_text(_lbl_date_draft, formatted.c_str());
    show_overlay(_dlg_date);
}

void Display::showQuantityEntry(const ProductInfo &product,
                                 const String &expiryDate, int quantity) {
    if (!_initialized) return;
    String info = product.name + "  MHD " + expiryDate;
    lv_label_set_text(_lbl_qty_product, info.c_str());
    lv_label_set_text(_lbl_qty_value, String(quantity).c_str());
    show_overlay(_dlg_qty);
}

void Display::showResult(const String &title, const String &message, bool success) {
    if (!_initialized) return;
    lv_label_set_text(_lbl_result_title, title.c_str());
    lv_obj_set_style_text_color(_lbl_result_title, success ? C_GREEN : C_RED, 0);
    lv_label_set_text(_lbl_result_msg, message.c_str());
    show_overlay(_dlg_result);
}

void Display::showInventoryList(const std::vector<InventoryItem> &items) {
    if (!_initialized) return;
    hide_overlay();
    show_tab(UiTab::INVENTORY);

    // Rebuild list content
    lv_obj_clean(_list_inventar);

    String count_str = String(items.size()) + " Einträge";
    lv_label_set_text(_lbl_inv_count, count_str.c_str());

    // Show newest items first, max 6 rows
    int shown = 0;
    int start = static_cast<int>(items.size()) - 1;
    for (int i = start; i >= 0 && shown < 20; i--, shown++) {
        const InventoryItem &item = items[i];

        lv_obj_t* row = lv_obj_create(_list_inventar);
        lv_obj_set_size(row, 472, 26);
        lv_obj_set_style_bg_color(row, (shown % 2 == 0) ? C_SURFACE : C_SURFACE2, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        struct { const char* text; int x; lv_color_t color; } cols[] = {
            { item.name.substring(0, 22).c_str(),       0,   C_TEXT    },
            { item.brand.substring(0, 12).c_str(),     155,  C_SUBTEXT },
            { item.expiryDate.c_str(),                 275,  C_TEXT    },
            { String(item.quantity).c_str(),           420,  C_ACCENT  },
        };
        for (auto &c : cols) {
            lv_obj_t* lbl = lv_label_create(row);
            lv_label_set_text(lbl, c.text);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl, c.color, 0);
            lv_obj_set_pos(lbl, c.x, 0);
        }
    }
}
