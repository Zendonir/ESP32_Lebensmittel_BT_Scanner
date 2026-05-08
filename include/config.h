#ifndef CONFIG_H
#define CONFIG_H

// ============ DISPLAY (ST7796 SPI) ============
#define LCD_MOSI    11
#define LCD_CLK     12
#define LCD_CS      10
#define LCD_DC      13
#define LCD_RST     9
#define LCD_BL      46

// Display resolution
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480

// ============ TOUCH (FT6336 I2C) ============
#define TOUCH_SDA   8
#define TOUCH_SCL   7
#define TOUCH_INT   4
#define TOUCH_ADDR  0x38
#define I2C_FREQ    400000

// ============ AUDIO (I2S) ============
#define I2S_BCLK    41
#define I2S_LRCK    42
#define I2S_DOUT    45
#define I2S_DIN     2
#define I2S_NUM     I2S_NUM_1

// ============ SD CARD (SPI) ============
#define SD_CS       1
#define SD_MOSI     11
#define SD_CLK      12
#define SD_MISO     14

// ============ UART (TTL Drucker/Scanner) ============
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
#define USER_BTN    3

// ============ WIFI ============
#define WIFI_SSID_STORAGE   "wifi_ssid"
#define WIFI_PASS_STORAGE   "wifi_pass"
#define AP_SSID             "ESP32-Scanner"
#define AP_PASSWORD         "12345678"

#endif
