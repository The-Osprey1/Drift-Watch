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

WakeCause doze(uint32_t t, bool lowPower) {
  if (!lowPower) {
    delay(t);
    return WakeCause::Timer;
  }
  // A line that is already low (finger still down, BOOT held, or a stuck touch line) would
  // wake the chip at once and keep it busy, so only lines that are idle (high) can wake it.
  // The caller polls touch and BOOT after every wake anyway.
  bool touchArmed = digitalRead(PIN_TP_INT) == HIGH, bootArmed = digitalRead(PIN_BOOT_BTN) == HIGH;
  if (touchArmed) gpio_wakeup_enable((gpio_num_t)PIN_TP_INT, GPIO_INTR_LOW_LEVEL);
  if (bootArmed) gpio_wakeup_enable((gpio_num_t)PIN_BOOT_BTN, GPIO_INTR_LOW_LEVEL);
  if (touchArmed || bootArmed) esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)t * 1000);
  Serial.flush();  // the serial port stops during light sleep and would cut a line short
  esp_light_sleep_start();
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (touchArmed) gpio_wakeup_disable((gpio_num_t)PIN_TP_INT);
  if (bootArmed) gpio_wakeup_disable((gpio_num_t)PIN_BOOT_BTN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (cause != ESP_SLEEP_WAKEUP_GPIO) return WakeCause::Timer;
  return digitalRead(PIN_BOOT_BTN) == LOW ? WakeCause::Button : WakeCause::Touch;
}

}  // namespace hal
