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
// Serial: UART0 on GPIO 43 (TX) and 44 (RX), carried to the USB-C port by a CH343P chip.
// The QMI8658 motion sensor is on the I2C pins above. The Waveshare pin table and the NuttX
// board notes both give its interrupt lines as INT1 = GPIO 4 and INT2 = GPIO 3 (GPIO 4 also
// drives the gate of the MOSFET1 solder pads). The firmware still polls the sensor; it only
// counts edges on these lines so a watch test can confirm the wiring (shown in Details).
#define PIN_IMU_INT1  4
#define PIN_IMU_INT2  3

// Screen turn on first start, as TFT_eSPI setRotation: 0 none, 1 a quarter turn clockwise,
// 2 upside down, 3 a quarter turn anticlockwise. It can be changed in Settings (Screen turn),
// and touch is lined up for the turn with a two-tap touch check (orient.h).
#define DISPLAY_ROTATION 1

// Which way the screen faces along the motion sensor's z axis before the watch has learned
// it (+1 or -1). The watch learns the real direction whenever the screen is touched.
#define IMU_SCREEN_FACING_DEFAULT 1

// Light sleep while the screen is off, waking on touch, BOOT or a timer (for raise-to-wake
// and the clock). Set to 0 if it misbehaves on a board; the screen-off wait is then a plain delay.
#ifndef DRIFT_LIGHT_SLEEP
#define DRIFT_LIGHT_SLEEP 1
#endif

#define FW_VERSION "0.12.0"

// Set to 1 to log frame and memory timings over USB serial and show them on the status panel.
#ifndef DRIFT_DIAG
#define DRIFT_DIAG 0
#endif
