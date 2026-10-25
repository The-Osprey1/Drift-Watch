#include "hal.h"
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <sys/time.h>
#include <stdarg.h>
#include "board.h"

namespace {
constexpr int BL_CHANNEL = 0;
constexpr int BL_FREQ = 5000;
}

namespace hal {

void begin() {
  Serial.begin(115200);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  analogReadResolution(12);
  ledcSetup(BL_CHANNEL, BL_FREQ, 8);
  ledcAttachPin(PIN_LCD_BL, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, 0);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
}

uint32_t ms() { return millis(); }
time_t now() { return time(nullptr); }
void idle(uint32_t t) { delay(t); }
void backlight(uint8_t level) { ledcWrite(BL_CHANNEL, level); }

int batteryMilliVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
  return mv * 3 / 8;  // 200k/100k divider
}

bool bootButtonDown() { return digitalRead(PIN_BOOT_BTN) == LOW; }

bool coldBoot() {
  esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_POWERON || r == ESP_RST_EXT || r == ESP_RST_SW || r == ESP_RST_UNKNOWN;
}

uint32_t freeHeap() { return ESP.getFreeHeap(); }
uint32_t freePsram() { return ESP.getFreePsram(); }

void log(const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.println(buf);
}

void setTime(time_t t) {
  timeval tv{t, 0};
  settimeofday(&tv, nullptr);
}

int64_t nowMs() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void setTimeMs(int64_t ms) {
  timeval tv{(time_t)(ms / 1000), (suseconds_t)(ms % 1000 * 1000)};
  settimeofday(&tv, nullptr);
}

void adjustMs(int64_t ms) { setTimeMs(nowMs() + ms); }

uint8_t doze(uint32_t t, const DozeArm &arm) {
  if (!arm.lowPower) {
    delay(t);
    return WAKE_TIMER;
  }
  // A line already at its wake level (finger still down, BOOT held, a stuck touch line, or a
  // motion line that did not clear) would wake the chip at once and keep it busy, so only idle
  // lines are armed. The caller polls touch and BOOT after every wake anyway.
  bool touchArmed = digitalRead(PIN_TP_INT) == HIGH, bootArmed = digitalRead(PIN_BOOT_BTN) == HIGH;
  bool motionArmed = arm.motion && digitalRead(PIN_IMU_INT2) == LOW;
  bool ok = true;
  if (touchArmed) ok &= gpio_wakeup_enable((gpio_num_t)PIN_TP_INT, GPIO_INTR_LOW_LEVEL) == ESP_OK;
  if (bootArmed) ok &= gpio_wakeup_enable((gpio_num_t)PIN_BOOT_BTN, GPIO_INTR_LOW_LEVEL) == ESP_OK;
  if (motionArmed) {
    // INT2 also has the CHANGE interrupt that counts its edges. Set to a level trigger it would
    // fire again and again while the line is high, so the interrupt is paused while armed;
    // waking from light sleep does not need it.
    gpio_intr_disable((gpio_num_t)PIN_IMU_INT2);
    ok &= gpio_wakeup_enable((gpio_num_t)PIN_IMU_INT2, GPIO_INTR_HIGH_LEVEL) == ESP_OK;
  }
  if (touchArmed || bootArmed || motionArmed) ok &= esp_sleep_enable_gpio_wakeup() == ESP_OK;
  ok &= esp_sleep_enable_timer_wakeup((uint64_t)t * 1000) == ESP_OK;
  uint8_t bits = 0;
  bool touchLow = false, bootLow = false, motionHigh = false;
  esp_err_t slept = ESP_FAIL;
  if (ok) {
    Serial.flush();  // the serial port stops during light sleep and would cut a line short
    slept = esp_light_sleep_start();
    // Read the lines straight away, before anything clears them: a touch pulse is short.
    touchLow = digitalRead(PIN_TP_INT) == LOW;
    bootLow = digitalRead(PIN_BOOT_BTN) == LOW;
    motionHigh = digitalRead(PIN_IMU_INT2) == HIGH;
  }
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (touchArmed) gpio_wakeup_disable((gpio_num_t)PIN_TP_INT);
  if (bootArmed) gpio_wakeup_disable((gpio_num_t)PIN_BOOT_BTN);
  if (motionArmed) {
    gpio_wakeup_disable((gpio_num_t)PIN_IMU_INT2);
    gpio_set_intr_type((gpio_num_t)PIN_IMU_INT2, GPIO_INTR_ANYEDGE);  // the edge counter again
    gpio_intr_enable((gpio_num_t)PIN_IMU_INT2);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (!ok) {
    delay(t);  // light sleep could not be set up: wait anyway, so the caller does not spin
    return WAKE_FAULT | WAKE_TIMER;
  }
  if (touchArmed && touchLow) bits |= WAKE_TOUCH;
  if (bootArmed && bootLow) bits |= WAKE_BUTTON;
  if (motionArmed && motionHigh) bits |= WAKE_MOTION;
  // Rejected: a wake line moved just before sleeping. The lines read above say which.
  // With nothing to show for it, pause briefly so a repeated rejection cannot spin.
  if (slept != ESP_OK) {
    if (bits) return bits;
    delay(t < 10 ? t : 10);
    return WAKE_TIMER;
  }
  if (cause == ESP_SLEEP_WAKEUP_GPIO && !(bits & (WAKE_TOUCH | WAKE_BUTTON | WAKE_MOTION))) {
    // A GPIO woke the chip but every line is idle again. The motion line stays high until it is
    // cleared and BOOT is held for much longer, so this was a short touch pulse.
    bits |= WAKE_TOUCH;
  }
  if (cause == ESP_SLEEP_WAKEUP_TIMER) bits |= WAKE_TIMER;
  return bits ? bits : WAKE_TIMER;
}

}  // namespace hal
