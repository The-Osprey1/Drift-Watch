#include "hal.h"
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
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

}  // namespace hal
