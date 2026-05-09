#ifndef CONFIG_H
#define CONFIG_H

// ============ DISPLAY (ST7796 SPI) ============
// Waveshare ESP32-S3-Touch-LCD-3.5 schematic:
// LCD_SPI_MOSI=GPIO1, LCD_SPI_MISO=GPIO2, LCD_DC=GPIO3,
// LCD_SPI_SCLK=GPIO5, LCD_BL=GPIO6. LCD_CS is not routed to a
// direct ESP32-S3 GPIO in the schematic, and LCD_RST is driven by the TCA9554
// expander, so keep both disabled for the TFT_eSPI direct-GPIO setup.
#define LCD_MOSI    1
#define LCD_MISO    2
#define LCD_DC      3
#define LCD_CLK     5
#define LCD_BL      6
#define LCD_CS      -1
#define LCD_RST     -1

// Native panel resolution (portrait) and logical UI resolution (landscape).
#define DISPLAY_WIDTH             320
#define DISPLAY_HEIGHT            480
#define DISPLAY_LANDSCAPE_WIDTH   480
#define DISPLAY_LANDSCAPE_HEIGHT  320

// ============ TOUCH (FT6336 I2C) ============
#define TOUCH_SDA   8
#define TOUCH_SCL   7
// Touch INT is connected through the TCA9554 expander (EXIO2), not directly
// to an ESP32-S3 GPIO. Keep it disabled until an expander driver is added.
#define TOUCH_INT   -1
#define TOUCH_ADDR  0x38
#define I2C_FREQ    400000

// ============ AUDIO (ES8311 / I2S) ============
#define I2S_MCLK    12
#define I2S_BCLK    13
#define I2S_LRCK    15
#define I2S_DOUT    14
#define I2S_DIN     16
#define I2S_NUM     I2S_NUM_1

// ============ SD CARD (SPI) ============
#define SD_MISO     9
#define SD_MOSI     10
#define SD_CLK      11
// SD CS is connected through the TCA9554 expander (EXIO3), not directly
// to an ESP32-S3 GPIO. Keep it disabled until an expander driver is added.
#define SD_CS       -1

// ============ UART (TTL Drucker) ============
#define UART_TX     43
#define UART_RX     44
#define UART_NUM    UART_NUM_1
#define UART_BAUD   9600

// ============ FREE GPIOs ============
#define FREE_GPIO_1 35
#define FREE_GPIO_2 36
#define FREE_GPIO_3 37

// ============ POWER/SYSTEM ============
#define BOOT_BTN    0
// PWR button is connected through the TCA9554 expander (EXIO6).
#define USER_BTN    -1

// ============ WIFI ============
#define WIFI_SSID_STORAGE   "wifi_ssid"
#define WIFI_PASS_STORAGE   "wifi_pass"
#define AP_SSID             "Lebensmittel-Scanner"
#define AP_PASSWORD         "12345678"

#endif
