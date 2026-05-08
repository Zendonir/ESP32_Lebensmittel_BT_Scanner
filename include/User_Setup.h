// TFT_eSPI User Setup für Waveshare ESP32-S3-Touch-LCD-3.5
// Dieses Setup wird automatisch in lv_conf.h konfiguriert

// ============ Display Driver Selection ============
#define ST7796_DRIVER

// ============ ESP32 Pins für ST7796 SPI ============
#define TFT_CS      10
#define TFT_DC      13
#define TFT_SDA     11  // MOSI
#define TFT_SCL     12  // CLK
#define TFT_RST     9
#define TFT_BL      46

// ============ SPI Setup ============
#define TFT_SPI_HOST    HSPI_HOST
#define TFT_SCLK_SPI    HSPI_SCLK
#define TFT_MOSI_SPI    HSPI_MOSI
#define TFT_MISO_SPI    -1

#define SPI_FREQUENCY   40000000
#define SPI_TOUCH_FREQUENCY 400000

// ============ Display Rotation (0-3) ============
#define TFT_ROTATION    0  // 0 = Portrait, 1 = Landscape

// ============ Display Colors ============
#define TFT_INVERSION_ON

// ============ Smooth Font Support ============
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

// ============ Anti-Aliasing (bessere Qualität) ============
#define USE_DMA_TO_TFT

// ============ Touch Support (FT6336) ============
#define TOUCH_CS   -1  // I2C Touch, kein CS benötigt
#define TOUCH_INT  4

// ============ Performance Options ============
#define READ_TOUCH_RAW
#define TOUCH_CALIB_MARGIN     15
#define TOUCH_CALIB_OFFSET1    55
#define TOUCH_CALIB_OFFSET2    40
#define TOUCH_CALIB_ROTATION   2
#define TOUCH_CALIB_X1         300
#define TOUCH_CALIB_Y1         300
#define TOUCH_CALIB_X2         3600
#define TOUCH_CALIB_Y2         3600
