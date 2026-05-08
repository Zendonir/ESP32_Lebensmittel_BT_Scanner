#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

class UI {
public:
    UI();
    void init();
    void createMainScreen();
    void createWiFiScreen();
    void createSettingsScreen();
    void updateWiFiStatus(bool connected, const char *ssid);
    void showMessage(const char *title, const char *message);
    void showLoading(bool show);

private:
    lv_obj_t *scr_main;
    lv_obj_t *scr_wifi;
    lv_obj_t *scr_settings;

    lv_obj_t *label_wifi_status;
    lv_obj_t *label_ssid;
    lv_obj_t *textarea_ssid;
    lv_obj_t *textarea_password;
    lv_obj_t *btn_wifi_connect;
    lv_obj_t *btn_ap_mode;

    static void onWiFiConnectClick(lv_event_t *e);
    static void onAPModeClick(lv_event_t *e);
    static void onSettingsClick(lv_event_t *e);
};

extern UI ui_obj;

#endif
