#include "hal.h"
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <sys/time.h>
#include <stdarg.h>
#include "board.h"

namespace {
constexpr int BL_CHANNEL = 0;
constexpr int BL_FREQ = 5000;

// Kept in memory that survives a restart but not a power loss, so the count covers one charge.
constexpr uint32_t RESTART_MAGIC = 0x44524654;  // "DRFT"
RTC_NOINIT_ATTR uint32_t restartMagic;
RTC_NOINIT_ATTR uint32_t restartCount;

// A device that was mid-transfer when the chip restarted can keep holding the data line low,
// and then every later transfer on the bus fails or waits for its timeout: the touch chip and
// motion sensor both look dead. Clocking up to nine pulses lets it finish its byte and let go,
// and a stop condition then leaves the bus idle. This is the usual I2C bus recovery.
void freeI2cBus() {
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(10);
  if (digitalRead(PIN_I2C_SDA) == HIGH && digitalRead(PIN_I2C_SCL) == HIGH) return;
  Serial.println("boot: I2C bus held low, freeing it");
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && digitalRead(PIN_I2C_SDA) == LOW; i++) {
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(5);
  }
  pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);  // stop: data rises while the clock is high
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SDA, HIGH);
  delayMicroseconds(5);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  Serial.printf("boot: I2C bus %s\n", digitalRead(PIN_I2C_SDA) == HIGH ? "free" : "still held");
}

bool selfRestart(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
         r == ESP_RST_BROWNOUT;
}
}

namespace hal {

void begin() {
  Serial.begin(115200);
  esp_reset_reason_t r = esp_reset_reason();
  if (r == ESP_RST_POWERON || restartMagic != RESTART_MAGIC) {
    restartMagic = RESTART_MAGIC;
    restartCount = 0;
  }
  if (selfRestart(r)) restartCount++;
  Serial.printf("\nboot: Drift-Watch v%s, start reason %d (%s)\n", FW_VERSION, (int)r, startReason() ? startReason() : "power on");
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  analogReadResolution(12);
  ledcSetup(BL_CHANNEL, BL_FREQ, 8);
  ledcAttachPin(PIN_LCD_BL, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, 0);
  freeI2cBus();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(20);  // a chip that stops answering costs 20 ms a transfer, not the default 50
}

uint32_t ms() { return millis(); }
time_t now() { return time(nullptr); }
void idle(uint32_t t) { delay(t); }
void backlight(uint8_t level) { ledcWrite(BL_CHANNEL, level); }

int batteryMilliVolts() {
  uint16_t samples[16];
  for (int i = 0; i < 16; i++) samples[i] = analogReadMilliVolts(PIN_BAT_ADC);
  for (int i = 1; i < 16; i++) {
    uint16_t key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }
  uint32_t sum = 0;
  for (int i = 3; i < 13; i++) sum += samples[i];
  return (sum * 3 + 5) / 10;  // 200k/100k divider (x3), rounded
}

bool bootButtonDown() { return digitalRead(PIN_BOOT_BTN) == LOW; }

bool coldBoot() {
  esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_POWERON || r == ESP_RST_EXT || r == ESP_RST_SW || r == ESP_RST_UNKNOWN;
}

const char *startReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC: return "Crashed, restarted";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "Froze, restarted by watchdog";
    case ESP_RST_BROWNOUT: return "Battery too low, restarted";
    case ESP_RST_SW: return "Restarted";
    case ESP_RST_DEEPSLEEP: return "Woke from deep sleep";
    default: return nullptr;  // power on, reset button
  }
}

uint32_t unexpectedRestarts() { return restartMagic == RESTART_MAGIC ? restartCount : 0; }

void watchdogStart(uint32_t seconds) {
  // The Arduino core already runs the task watchdog for the idle tasks; this sets its time limit,
  // makes a timeout restart the chip, and adds the loop to what it watches.
  esp_err_t a = esp_task_wdt_init(seconds, true), b = esp_task_wdt_add(nullptr);
  Serial.printf("boot: watchdog %lu s (%s)\n", (unsigned long)seconds, a == ESP_OK && b == ESP_OK ? "on" : "could not start");
}

void watchdogFeed() { esp_task_wdt_reset(); }

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
  // Serial data: the USB-C port's chip talks to UART0 (RX on GPIO 44). In light sleep the UART is
  // stopped and bytes are dropped, so a sync sent to a dark watch used to vanish. The UART wakes
  // the chip after 3 rising edges on RX (one newline has 3); those bytes are lost, so the
  // computer sends a newline first and retries (tools/windows, companion). A failure here only
  // loses this wake source, never light sleep itself.
  bool serialArmed = arm.serial && uart_set_wakeup_threshold(UART_NUM_0, 3) == ESP_OK &&
                     esp_sleep_enable_uart_wakeup(UART_NUM_0) == ESP_OK;
  ok &= esp_sleep_enable_timer_wakeup((uint64_t)t * 1000) == ESP_OK;
  uint8_t bits = 0;
  bool touchLow = false, bootLow = false, motionHigh = false;
  esp_err_t slept = ESP_FAIL;
  if (ok) {
    Serial.flush();  // the serial port stops during light sleep and would cut a line short
    esp_task_wdt_reset();
    slept = esp_light_sleep_start();
    esp_task_wdt_reset();
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
  if (serialArmed && cause == ESP_SLEEP_WAKEUP_UART) bits |= WAKE_SERIAL;
  if (cause == ESP_SLEEP_WAKEUP_TIMER) bits |= WAKE_TIMER;
  return bits ? bits : WAKE_TIMER;
}

}  // namespace hal
