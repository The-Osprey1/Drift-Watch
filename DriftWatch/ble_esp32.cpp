#include "ble.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_bt_main.h>
#include <esp_mac.h>
#include <freertos/queue.h>
#include "hal.h"
#include "phone.h"

namespace {
constexpr const char *SERVICE_UUID = "8f1d0001-6b5c-4c8e-9d3a-2a6b7c8d9e01";
constexpr const char *COMMAND_UUID = "8f1d0002-6b5c-4c8e-9d3a-2a6b7c8d9e01";
constexpr const char *STATUS_UUID = "8f1d0003-6b5c-4c8e-9d3a-2a6b7c8d9e01";
constexpr uint16_t CTS_SERVICE = 0x1805, CTS_CURRENT_TIME = 0x2A2B, CTS_LOCAL_TIME_INFO = 0x2A0F;
constexpr size_t LINE = 128;   // longest line kept; the Drift sync page sends far less
constexpr int QUEUE_DEPTH = 8;

struct Line { char text[LINE]; };

ble::State st = ble::State::Off;
bool started = false;
uint32_t openedAt = 0;
volatile uint32_t lastLineAt = 0;     // written by the Bluetooth task
volatile bool connected = false;
QueueHandle_t lines = nullptr;
BLEServer *server = nullptr;
BLECharacteristic *statusChar = nullptr;
char deviceName[16] = "Drift";
char part[LINE];
size_t partLen = 0;

// Runs on the Bluetooth task: copy the line and leave, the main loop applies it.
void queueLine(const char *text, size_t n) {
  if (!lines) return;
  Line l;
  n = n < LINE - 1 ? n : LINE - 1;
  memcpy(l.text, text, n);
  l.text[n] = 0;
  while (n && (l.text[n - 1] == '\n' || l.text[n - 1] == '\r')) l.text[--n] = 0;
  if (xQueueSend(lines, &l, 0) == pdTRUE) lastLineAt = millis();
}

void appendPart(const char *data, size_t n) {
  size_t room = LINE - 1 - partLen;
  if (n > room) n = room;
  if (n) memcpy(part + partLen, data, n);
  partLen += n;
}

class CommandWrites : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    std::string v = c->getValue();
    // 0x01 marks a piece with more to come and 0x02 marks the last piece, for iOS Web Bluetooth
    // wrappers that split writes at 20 bytes; ordinary clients send one whole line per write.
    if (!v.empty() && (uint8_t)v[0] == 0x01) {
      appendPart(v.data() + 1, v.size() - 1);
    } else if (!v.empty() && (uint8_t)v[0] == 0x02) {
      appendPart(v.data() + 1, v.size() - 1);
      queueLine(part, partLen);
      partLen = 0;
    } else {
      partLen = 0;
      queueLine(v.data(), v.size());
    }
  }
};

class TimeWrites : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    std::string v = c->getValue();
    char text[40];
    bool ok = c->getUUID().equals(BLEUUID(CTS_CURRENT_TIME))
                  ? phone::fromCurrentTime((const uint8_t *)v.data(), v.size(), text, sizeof text)
                  : phone::fromLocalTimeInfo((const uint8_t *)v.data(), v.size(), text, sizeof text);
    if (ok) queueLine(text, strlen(text));
  }
};

class Links : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { connected = true; }
  void onDisconnect(BLEServer *s) override {
    connected = false;
    partLen = 0;
    s->startAdvertising();  // the window is still open: let the phone come back
  }
};

CommandWrites commandWrites;
TimeWrites timeWrites;
Links links;

bool start() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(deviceName, sizeof deviceName, "Drift-%02X%02X", mac[4], mac[5]);
  BLEDevice::init(deviceName);
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) return false;
  // Without this, Bluedroid keeps the 23-byte default ATT MTU (20 bytes of payload), cutting
  // Z=, N=, S= and P= lines and status notifications short on iPhones and browsers.
  BLEDevice::setMTU(247);
  // The objects below belong to this start; Bluetooth is shut down with deinit(false) on
  // close, so a later open builds a fresh set (the old ones are a few hundred bytes).
  server = BLEDevice::createServer();
  server->setCallbacks(&links);

  BLEService *drift = server->createService(SERVICE_UUID);
  // Acknowledged writes only: unacknowledged writes cannot use GATT long writes and are cut off
  // or refused past MTU - 3 bytes by iOS Web Bluetooth browsers.
  BLECharacteristic *cmd = drift->createCharacteristic(COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE);
  cmd->setCallbacks(&commandWrites);
  statusChar = drift->createCharacteristic(STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());
  statusChar->setValue("fw=starting");
  drift->start();

  BLEService *cts = server->createService(BLEUUID(CTS_SERVICE));
  BLECharacteristic *now = cts->createCharacteristic(
      BLEUUID(CTS_CURRENT_TIME), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  now->setCallbacks(&timeWrites);
  BLECharacteristic *info = cts->createCharacteristic(
      BLEUUID(CTS_LOCAL_TIME_INFO), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  info->setCallbacks(&timeWrites);
  cts->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);  // the 128-bit service and the name do not both fit in the advert
  adv->setMinPreferred(0x06);  // 7.5 ms to 22.5 ms connection interval, per Apple's BLE guidelines
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  return true;
}
}  // namespace

namespace ble {

void open() {
  openedAt = hal::ms();
  lastLineAt = 0;
  if (started) return;
  partLen = 0;
  if (!lines) lines = xQueueCreate(QUEUE_DEPTH, sizeof(Line));
  xQueueReset(lines);
  started = start();
  st = started ? State::Waiting : State::Failed;
  hal::log("ble: %s as %s, free heap %lu", started ? "advertising" : "did not start", deviceName,
           (unsigned long)hal::freeHeap());
  if (!started) BLEDevice::deinit(false);
}

void close() {
  if (!started) {
    if (st == State::Failed) st = State::Off;
    return;
  }
  BLEDevice::stopAdvertising();
  BLEDevice::deinit(false);  // keeps the memory so Bluetooth can start again later
  server = nullptr;
  statusChar = nullptr;
  started = false;
  connected = false;
  st = State::Off;
  hal::log("ble: off, free heap %lu", (unsigned long)hal::freeHeap());
}

void poll() {
  if (!started) return;
  st = connected ? State::Connected : State::Waiting;
  if (!secondsLeft()) close();
}

State state() { return st; }
bool on() { return started; }

uint32_t secondsLeft() {
  if (!started) return 0;
  uint32_t from = lastLineAt ? lastLineAt : openedAt, used = hal::ms() - from;
  return used >= WINDOW_MS ? 0 : (WINDOW_MS - used + 999) / 1000;
}

const char *name() { return deviceName; }

bool nextLine(char *out, size_t n) {
  Line l;
  if (!lines || xQueueReceive(lines, &l, 0) != pdTRUE) return false;
  strncpy(out, l.text, n - 1);
  out[n - 1] = 0;
  return true;
}

void setStatus(const char *text) {
  if (!statusChar) return;
  statusChar->setValue(text);
  if (connected) statusChar->notify();
}

}  // namespace ble
