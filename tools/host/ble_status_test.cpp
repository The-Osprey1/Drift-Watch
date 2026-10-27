// Pure status framing/queue tests; no fonts, Arduino runtime or radio required.
// g++ -std=c++17 -Wall -Wextra -Werror -I DriftWatch tools/host/ble_status_test.cpp -o ble_status_test
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "ble_status_frames.h"

namespace {
int checks = 0, failures = 0;

void check(bool condition, const char *message) {
  checks++;
  if (!condition) {
    failures++;
    printf("FAIL: %s\n", message);
  }
}

struct Seen {
  bleframes::Kind kind;
  std::string text, fullStatus;
};

std::vector<Seen> drain(bleframes::StatusQueue &queue) {
  std::vector<Seen> frames;
  bleframes::Frame frame;
  while (queue.next(frame)) {
    check(frame.fullStatus != nullptr, "every emitted frame retains its complete readable status");
    check(frame.length == strlen(frame.text), "frame length describes the complete string");
    check(frame.length <= bleframes::SMALL_PAYLOAD,
          "every notification fits the minimum ATT MTU without truncation");
    frames.push_back({frame.kind, frame.text, frame.fullStatus ? frame.fullStatus : ""});
    if (frames.size() > 1000) { check(false, "queue iteration is bounded"); break; }
  }
  check(queue.size() == 0, "drained snapshots leave an empty queue");
  return frames;
}

bool hasField(const std::vector<Seen> &frames, const std::string &text) {
  for (const Seen &f : frames)
    if (f.kind == bleframes::Kind::Field && f.text == text) return true;
  return false;
}

void fullStatus() {
  const char *status =
      "reply=ok;fw=0.16.0;time=1800000000;tz=GMT0BST,M3.5.0/1,M10.5.0;"
      "wifi=an-arbitrarily-long-wireless-network;bat=86;face=2;faces=4095;bright=2;"
      "motion=1;format=24;night=0;proto=1;caps=0x1f;mv=4082;temp=28.4;"
      "drift=-30000000;timeq=synced;source=usb;syncage=4294967295;"
      "timer=interrupted;remain=86400";
  bleframes::StatusQueue queue;
  check(queue.push(status), "extended status fits a snapshot");
  auto frames = drain(queue);
  check(!frames.empty() && frames.front().kind == bleframes::Kind::Field,
        "extended status starts with a complete field instead of a truncatable raw notification");
  check(frames.back().kind == bleframes::Kind::Acknowledgement && frames.back().text == "reply=ok;",
        "successful acknowledgement is delivered last");
  for (const Seen &f : frames) check(f.fullStatus == status, "every frame restores the complete original snapshot");
  const char *expected[] = {
      "fw=0.16.0", "time=1800000000", "bat=86", "face=2", "faces=4095", "bright=2",
      "motion=1", "format=24", "night=0", "proto=1", "caps=0x1f", "mv=4082", "temp=28.4",
      "drift=-30000000", "timeq=synced", "source=usb", "syncage=4294967295",
      "timer=interrupted", "remain=86400"};
  size_t index = 0;
  for (const char *field : expected) {
    check(index < frames.size() && frames[index].kind == bleframes::Kind::Field &&
              frames[index].text == field, "known short fields are complete and retain snapshot order");
    index++;
  }
  check(frames.size() == sizeof expected / sizeof expected[0] + 1,
        "long raw/zone/network values are skipped rather than truncated");
}

void replySafety() {
  bleframes::StatusQueue queue;
  check(queue.push("reply=B= out of range;fw=0.16.0;bat=-1"), "error snapshot is accepted");
  auto frames = drain(queue);
  check(frames.back().text == "reply=error;", "long refusal has a complete short error acknowledgement");
  check(hasField(frames, "bat=-1"), "unavailable battery value stays a complete field");

  check(queue.push("reply=ok;reply=error;fw=0.16.0;RePlY=ok;mv=0;reply=ok"), "embedded reply tokens are accepted as ignored data");
  frames = drain(queue);
  check(frames.front().text == "fw=0.16.0;mv=0", "all embedded reply tokens are removed from the raw notification");
  check(frames.size() == 4 && frames.back().text == "reply=ok;", "only the leading reply token owns the final ACK");
  check(queue.push("fw=0.16.0;reply=ok;mv=0;REPLY=error"), "snapshot without leading reply is accepted");
  frames = drain(queue);
  for (const Seen &f : frames) check(f.kind != bleframes::Kind::Acknowledgement, "embedded replies cannot manufacture an ACK");
  check(frames.front().text == "fw=0.16.0;mv=0", "embedded reply fields never reach raw-only clients");

  check(queue.push("reply=ok;"), "reply-only status is accepted");
  frames = drain(queue);
  check(frames.size() == 1 && frames[0].text == "reply=ok;", "reply-only status emits exactly one ACK");
  check(queue.push("reply=ok trailing;fw=0.16.0"), "nonexact reply value can be represented");
  frames = drain(queue);
  check(frames.back().text == "reply=error;", "a partial ok value is never promoted to success");
}

void payloadLimits() {
  bleframes::StatusQueue queue;
  const std::string exactly20 = "wifi=" + std::string(15, 'a');
  const std::string tooLong = "tz=" + std::string(18, 'b');
  const std::string utf8 = std::string("wifi=") + "\xE2\x82\xAC" + std::string(12, 'c');
  check(exactly20.size() == 20 && utf8.size() == 20 && tooLong.size() == 21, "test fields straddle ATT payload bounds");
  const std::string status = "reply=ok;" + exactly20 + ";" + tooLong + ";" + utf8 + ";unknown=1;MV=4200";
  check(queue.push(status.c_str()), "boundary-field snapshot is accepted");
  auto frames = drain(queue);
  check(hasField(frames, exactly20), "a complete 20-byte field is delivered");
  check(hasField(frames, utf8), "UTF-8 field is delivered whole without splitting a code point");
  check(!hasField(frames, tooLong), "21-byte value is skipped instead of truncated");
  check(!hasField(frames, "unknown=1"), "unknown keys are omitted from short frames");
  check(hasField(frames, "MV=4200"), "known keys are recognized case-insensitively");
  for (const Seen &f : frames) check(f.kind != bleframes::Kind::Raw, "an oversized raw notification is never emitted");
  check(frames.front().fullStatus.find("unknown=1") != std::string::npos,
        "complete readable status retains non-reply future fields");

  std::string boundary = "reply=ok;fw=0.16.0;wifi=";
  boundary += std::string(bleframes::MAX_SNAPSHOT - boundary.size(), 'x');
  check(boundary.size() == bleframes::MAX_SNAPSHOT && queue.push(boundary.c_str()),
        "exactly 512 text bytes fit without dropping the ACK");
  frames = drain(queue);
  check(frames.back().text == "reply=ok;" && frames.back().fullStatus == boundary,
        "maximum-length snapshot remains complete for a GATT read");
  boundary += 'x';
  check(!queue.push(boundary.c_str()) && queue.size() == 0, "oversized snapshot is dropped without an ACK");
  check(!queue.push(nullptr) && !queue.push("") && queue.size() == 0, "null/empty snapshots cannot create queued frames");
}

void queueBoundsAndReset() {
  bleframes::StatusQueue queue;
  for (unsigned i = 0; i < bleframes::QUEUE_DEPTH; i++) {
    const std::string status = "reply=ok;time=" + std::to_string(i);
    check(queue.push(status.c_str()), "each supported queue slot accepts a snapshot");
  }
  check(queue.size() == bleframes::QUEUE_DEPTH, "queue reaches the documented eight-snapshot bound");
  check(!queue.push("reply=ok;time=999"), "overflow drops the new snapshot");
  auto frames = drain(queue);
  unsigned acknowledgements = 0, expected = 0;
  for (const Seen &f : frames) {
    check(f.fullStatus.find("999") == std::string::npos, "overflow snapshot never appears as data or acknowledgement");
    if (f.kind == bleframes::Kind::Acknowledgement) {
      check(f.fullStatus == "reply=ok;time=" + std::to_string(expected), "snapshots and their acknowledgements retain FIFO order");
      expected++;
      acknowledgements++;
    }
  }
  check(acknowledgements == bleframes::QUEUE_DEPTH, "only accepted snapshots receive acknowledgements");

  check(queue.push("reply=ok;time=1"), "queue can be reused after wraparound");
  bleframes::Frame partial;
  check(queue.next(partial) && partial.kind == bleframes::Kind::Raw, "partial snapshot began before close/reset");
  queue.clear();
  check(queue.size() == 0 && !queue.next(partial), "close/session reset discards pending fields and ACK");
  check(queue.push("reply=ok;time=2"), "fresh session can enqueue after clear");
  frames = drain(queue);
  for (const Seen &f : frames) check(f.fullStatus == "reply=ok;time=2", "fresh session cannot receive old status or ACK");
}
}  // namespace

int main() {
  fullStatus();
  replySafety();
  payloadLimits();
  queueBoundsAndReset();
  printf("BLE status: %d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
