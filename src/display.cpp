#include "display.h"

Display display_obj;

static uint8_t buf1[DISPLAY_WIDTH * DISPLAY_HEIGHT * 2 / 10];
static uint8_t buf2[DISPLAY_WIDTH * DISPLAY_HEIGHT * 2 / 10];

Display::Display() : backlight_level(255) {}

void Display::init() {
    pinMode(LCD_BL, OUTPUT);
    setBacklight(255);

    initSPI();
    initLVGL();
}

void Display::initSPI() {
    hspi = new SPIClass(HSPI);
    hspi->begin(LCD_CLK, -1, LCD_MOSI, LCD_CS);

    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_RST, OUTPUT);
    pinMode(LCD_CS, OUTPUT);

    // Reset display
    digitalWrite(LCD_RST, LOW);
    delay(100);
    digitalWrite(LCD_RST, HIGH);
    delay(100);
}

void Display::initLVGL() {
    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DISPLAY_WIDTH * DISPLAY_HEIGHT / 10);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = Display::flushCB;
    disp_drv.user_data = this;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = Display::readCB;
    indev_drv.user_data = this;
    lv_indev_drv_register(&indev_drv);
}

void Display::flushCB(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    Display *this_ptr = (Display *)disp->user_data;

    // Write color data to display
    // This is a simplified version - actual implementation depends on ST7796 driver
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    uint32_t len = w * h;

    this_ptr->hspi->beginTransaction(SPISettings(40000000, SPI_MSBFIRST, SPI_MODE0));

    digitalWrite(LCD_DC, LOW);
    digitalWrite(LCD_CS, LOW);
    this_ptr->hspi->write16(area->x1);
    this_ptr->hspi->write16(area->y1);
    this_ptr->hspi->write16(area->x2);
    this_ptr->hspi->write16(area->y2);
    digitalWrite(LCD_CS, HIGH);

    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    for (uint32_t i = 0; i < len; i++) {
        this_ptr->hspi->write16(color_p[i].full);
    }
    digitalWrite(LCD_CS, HIGH);

    this_ptr->hspi->endTransaction();

    lv_disp_flush_ready(disp);
}

void Display::readCB(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    // Touch reading is handled by Touch class
    data->point.x = 0;
    data->point.y = 0;
    data->state = LV_INDEV_STATE_REL;
}

void Display::setBacklight(uint8_t brightness) {
    backlight_level = brightness;
    ledcWrite(0, brightness);
}

void Display::update() {
    lv_timer_handler();
}
