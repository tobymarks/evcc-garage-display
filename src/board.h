#pragma once

// Guition ESP32-4848S040C: 4" IPS 480x480, ST7701S über 16-Bit-RGB, Touch GT911 (I²C)
// Pins und Timings wie in Arduino_GFX (ESP32_4848S040_86BOX_GUITION) und der ESPHome-Gerätedoku.

#define LCD_WIDTH 480
#define LCD_HEIGHT 480
#define LCD_PCLK_HZ (12 * 1000 * 1000)

#define LCD_HSYNC_PULSE 8
#define LCD_HSYNC_BACK 50
#define LCD_HSYNC_FRONT 10
#define LCD_VSYNC_PULSE 8
#define LCD_VSYNC_BACK 20
#define LCD_VSYNC_FRONT 10

#define PIN_LCD_DE 18
#define PIN_LCD_VSYNC 17
#define PIN_LCD_HSYNC 16
#define PIN_LCD_PCLK 21
// Reihenfolge für esp_lcd: B0–B4, G0–G5, R0–R4
#define LCD_DATA_PINS {4, 5, 6, 7, 15, 8, 20, 3, 46, 9, 10, 11, 12, 13, 14, 0}

// 3-Draht-SPI (9 Bit) nur zur Initialisierung des ST7701S
#define PIN_LCD_SPI_CS 39
#define PIN_LCD_SPI_SCK 48
#define PIN_LCD_SPI_MOSI 47

#define PIN_LCD_BACKLIGHT 38

#define PIN_TOUCH_SDA 19
#define PIN_TOUCH_SCL 45
