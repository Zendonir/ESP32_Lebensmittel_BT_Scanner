#include "ui.h"
#include "wifi_manager.h"
#include "audio.h"

UI ui_obj;

UI::UI() {}

void UI::init() {
    createMainScreen();
}

void UI::createMainScreen() {
    scr_main = lv_scr_act();
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x1a1a2e), 0);

    // Title
    lv_obj_t *title = lv_label_create(scr_main);
    lv_label_set_text(title, "ESP32 Scanner");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // WiFi Status Label
    label_wifi_status = lv_label_create(scr_main);
    lv_label_set_text(label_wifi_status, "WiFi: Not Connected");
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xFF6B6B), 0);
    lv_obj_align_to(label_wifi_status, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);

    // SSID Label
    label_ssid = lv_label_create(scr_main);
    lv_label_set_text(label_ssid, "SSID: None");
    lv_obj_set_style_text_color(label_ssid, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(label_ssid, label_wifi_status, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 15);

    // WiFi Button
    lv_obj_t *btn_wifi = lv_btn_create(scr_main);
    lv_obj_set_width(btn_wifi, 280);
    lv_obj_set_height(btn_wifi, 50);
    lv_obj_align_to(btn_wifi, label_ssid, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0x4ECDC4), 0);

    lv_obj_t *label = lv_label_create(btn_wifi);
    lv_label_set_text(label, "WiFi Settings");
    lv_obj_center(label);

    lv_obj_add_event_cb(btn_wifi, onWiFiConnectClick, LV_EVENT_CLICKED, NULL);

    // AP Mode Button
    lv_obj_t *btn_ap = lv_btn_create(scr_main);
    lv_obj_set_width(btn_ap, 280);
    lv_obj_set_height(btn_ap, 50);
    lv_obj_align_to(btn_ap, btn_wifi, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_style_bg_color(btn_ap, lv_color_hex(0x44AF69), 0);

    label = lv_label_create(btn_ap);
    lv_label_set_text(label, "Start AP Mode");
    lv_obj_center(label);

    lv_obj_add_event_cb(btn_ap, onAPModeClick, LV_EVENT_CLICKED, NULL);

    // Settings Button
    lv_obj_t *btn_settings = lv_btn_create(scr_main);
    lv_obj_set_width(btn_settings, 280);
    lv_obj_set_height(btn_settings, 50);
    lv_obj_align_to(btn_settings, btn_ap, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0xF39C12), 0);

    label = lv_label_create(btn_settings);
    lv_label_set_text(label, "Settings");
    lv_obj_center(label);

    lv_obj_add_event_cb(btn_settings, onSettingsClick, LV_EVENT_CLICKED, NULL);
}

void UI::createWiFiScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, lv_color_hex(0x1a1a2e), 0);

    // Back button
    lv_obj_t *btn_back = lv_btn_create(scr_wifi);
    lv_obj_set_width(btn_back, 60);
    lv_obj_set_height(btn_back, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *label = lv_label_create(btn_back);
    lv_label_set_text(label, "Back");
    lv_obj_center(label);

    // SSID Input
    lv_obj_t *label_ssid_title = lv_label_create(scr_wifi);
    lv_label_set_text(label_ssid_title, "SSID:");
    lv_obj_align(label_ssid_title, LV_ALIGN_TOP_LEFT, 20, 70);

    textarea_ssid = lv_textarea_create(scr_wifi);
    lv_textarea_set_placeholder_text(textarea_ssid, "Enter SSID");
    lv_obj_set_width(textarea_ssid, 280);
    lv_obj_set_height(textarea_ssid, 45);
    lv_obj_align(textarea_ssid, LV_ALIGN_TOP_MID, 0, 100);

    // Password Input
    lv_obj_t *label_pass_title = lv_label_create(scr_wifi);
    lv_label_set_text(label_pass_title, "Password:");
    lv_obj_align(label_pass_title, LV_ALIGN_TOP_LEFT, 20, 160);

    textarea_password = lv_textarea_create(scr_wifi);
    lv_textarea_set_placeholder_text(textarea_password, "Enter Password");
    lv_textarea_set_password_mode(textarea_password, true);
    lv_obj_set_width(textarea_password, 280);
    lv_obj_set_height(textarea_password, 45);
    lv_obj_align(textarea_password, LV_ALIGN_TOP_MID, 0, 190);

    // Connect Button
    btn_wifi_connect = lv_btn_create(scr_wifi);
    lv_obj_set_width(btn_wifi_connect, 280);
    lv_obj_set_height(btn_wifi_connect, 50);
    lv_obj_align(btn_wifi_connect, LV_ALIGN_CENTER, 0, 120);
    lv_obj_set_style_bg_color(btn_wifi_connect, lv_color_hex(0x4ECDC4), 0);

    label = lv_label_create(btn_wifi_connect);
    lv_label_set_text(label, "Connect");
    lv_obj_center(label);
}

void UI::createSettingsScreen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, lv_color_hex(0x1a1a2e), 0);

    lv_obj_t *title = lv_label_create(scr_settings);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Volume slider
    lv_obj_t *label_vol = lv_label_create(scr_settings);
    lv_label_set_text(label_vol, "Volume:");
    lv_obj_align(label_vol, LV_ALIGN_TOP_LEFT, 20, 70);

    lv_obj_t *slider = lv_slider_create(scr_settings);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_set_width(slider, 280);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 110);

    // Brightness slider
    lv_obj_t *label_bright = lv_label_create(scr_settings);
    lv_label_set_text(label_bright, "Brightness:");
    lv_obj_align(label_bright, LV_ALIGN_TOP_LEFT, 20, 160);

    slider = lv_slider_create(scr_settings);
    lv_slider_set_range(slider, 0, 255);
    lv_slider_set_value(slider, 255, LV_ANIM_OFF);
    lv_obj_set_width(slider, 280);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 200);
}

void UI::updateWiFiStatus(bool connected, const char *ssid) {
    if (connected) {
        lv_label_set_text(label_wifi_status, "WiFi: Connected");
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x4ECDC4), 0);
    } else {
        lv_label_set_text(label_wifi_status, "WiFi: Not Connected");
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xFF6B6B), 0);
    }

    static char ssid_text[64];
    snprintf(ssid_text, sizeof(ssid_text), "SSID: %s", ssid);
    lv_label_set_text(label_ssid, ssid_text);
}

void UI::showMessage(const char *title, const char *message) {
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title_text(mbox, title);
    lv_msgbox_add_text(mbox, message);
    lv_obj_center(mbox);
}

void UI::showLoading(bool show) {
    // Could create a spinner or loading animation
    if (show) {
        // Show spinner
    } else {
        // Hide spinner
    }
}

void UI::onWiFiConnectClick(lv_event_t *e) {
    // Navigate to WiFi settings screen
    lv_scr_load(ui_obj.scr_wifi);
    audio_obj.playTone(1000, 100);
}

void UI::onAPModeClick(lv_event_t *e) {
    wifi_manager.startAP();
    ui_obj.updateWiFiStatus(false, "AP Active");
    audio_obj.playTone(2000, 200);
}

void UI::onSettingsClick(lv_event_t *e) {
    lv_scr_load(ui_obj.scr_settings);
    audio_obj.playTone(1500, 100);
}
