// Pin map for the Waveshare ESP32-S3-Touch-LCD-1.28 (ESP32-S3R2, 2 MB PSRAM,
// 16 MB flash, GC9A01A 240 x 240 IPS LCD on SPI, CST816S touch on I2C).
// Checked against the Waveshare wiki pin table. The screen pins live in
// build.sh because TFT_eSPI reads them from compiler flags.
#pragma once

#define PIN_LCD_BL    2    // backlight, driven with PWM
#define PIN_I2C_SDA   6    // shared by the touch chip and the motion sensor
#define PIN_I2C_SCL   7
#define PIN_TP_INT    5    // CST816S touch interrupt
#define PIN_TP_RST    13   // CST816S touch reset
#define PIN_BAT_ADC   1    // battery through a 200k/100k divider (x3)
#define PIN_BOOT_BTN  0    // BOOT button, also usable as a normal button

// Touch axes relative to the screen. Flip these if swipes run the wrong way on the real watch.
#define TOUCH_SWAP_XY 0
#define TOUCH_FLIP_X  0
#define TOUCH_FLIP_Y  0

#define FW_VERSION "0.7.0"

// Set to 1 to log frame and memory timings over USB serial and show them on the status panel.
#ifndef DRIFT_DIAG
#define DRIFT_DIAG 0
#endif
