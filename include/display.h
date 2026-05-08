#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <SPI.h>
#include <lvgl.h>
#include "config.h"

class Display {
public:
    Display();
    void init();
    void setBacklight(uint8_t brightness);
    void update();
    static void flushCB(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
    static void readCB(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

private:
    SPIClass *hspi;
    uint8_t backlight_level;
    lv_disp_draw_buf_t draw_buf;
    lv_disp_drv_t disp_drv;
    lv_indev_drv_t indev_drv;

    void initSPI();
    void initLVGL();
};

extern Display display_obj;

#endif
