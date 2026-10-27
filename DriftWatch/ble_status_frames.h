// Main-loop-owned BLE status snapshots. A raw status is notified only when it
// fits the minimum ATT MTU; larger snapshots use complete small fields instead.
// The complete status remains readable. ACK follows all fields for its snapshot.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bleframes {

constexpr size_t MAX_SNAPSHOT = 512;  // text bytes, excluding the terminating NUL
constexpr size_t SMALL_PAYLOAD = 20;  // MTU 23 minus ATT notification overhead
constexpr size_t QUEUE_DEPTH = 8;

enum class Kind : uint8_t { Raw, Field, Acknowledgement };

struct Frame {
  char text[MAX_SNAPSHOT + 1];
  size_t length = 0;
  Kind kind = Kind::Raw;
  // Valid until the queue slot is reused. Restore this after notifying text so
  // a GATT long read sees the complete status, never the temporary small field.
  const char *fullStatus = nullptr;
};

class StatusQueue {
 public:
  bool push(const char *text) {
    if (!text || count_ == QUEUE_DEPTH) return false;
    size_t length = 0;
    while (length <= MAX_SNAPSHOT && text[length]) length++;
    if (!length || length > MAX_SNAPSHOT) return false;
    Snapshot &s = snapshots_[(head_ + count_) % QUEUE_DEPTH];
    memcpy(s.text, text, length + 1);
    s.length = length;
    s.offset = 0;
    s.rawPending = true;
    s.ack = Ack::None;
    // Only the leading reply token owns an acknowledgement. A reply token
    // embedded in another status is data to drop, never another command ACK.
    const char *firstEnd = (const char *)memchr(text, ';', length);
    size_t firstLength = firstEnd ? (size_t)(firstEnd - text) : length;
    if (keyIs(text, firstLength, "reply")) {
      const char *value = (const char *)memchr(text, '=', firstLength) + 1;
      size_t valueLength = firstLength - (size_t)(value - text);
      s.ack = valueLength == 2 && !memcmp(value, "ok", 2) ? Ack::Ok : Ack::Error;
    }
    count_++;
    return true;
  }

  bool next(Frame &frame) {
    frame.length = 0;
    frame.text[0] = 0;
    frame.fullStatus = nullptr;
    while (count_) {
      Snapshot &s = snapshots_[head_];
      if (s.rawPending) {
        s.rawPending = false;
        size_t written = 0, at = 0;
        while (at < s.length) {
          const size_t end = tokenEnd(s, at);
          const size_t length = end - at;
          if (length && !keyIs(s.text + at, length, "reply")) {
            if (written) frame.text[written++] = ';';
            memcpy(frame.text + written, s.text + at, length);
            written += length;
          }
          at = end < s.length ? end + 1 : end;
        }
        if (written && written <= SMALL_PAYLOAD) {
          frame.text[written] = 0;
          frame.length = written;
          frame.kind = Kind::Raw;
          frame.fullStatus = s.text;
          return true;
        }
      }
      while (s.offset < s.length) {
        const size_t at = s.offset, end = tokenEnd(s, at);
        const size_t length = end - at;
        s.offset = end < s.length ? end + 1 : end;
        if (!length || length > SMALL_PAYLOAD || !knownKey(s.text + at, length)) continue;
        memcpy(frame.text, s.text + at, length);
        frame.text[length] = 0;
        frame.length = length;
        frame.kind = Kind::Field;
        frame.fullStatus = s.text;
        return true;
      }
      if (s.ack != Ack::None) {
        const char *ack = s.ack == Ack::Ok ? "reply=ok;" : "reply=error;";
        strcpy(frame.text, ack);
        frame.length = strlen(ack);
        frame.kind = Kind::Acknowledgement;
        frame.fullStatus = s.text;
        pop();
        return true;
      }
      pop();
    }
    return false;
  }

  void clear() { head_ = count_ = 0; }
  size_t size() const { return count_; }

 private:
  enum class Ack : uint8_t { None, Ok, Error };
  struct Snapshot {
    char text[MAX_SNAPSHOT + 1];
    size_t length = 0, offset = 0;
    bool rawPending = true;
    Ack ack = Ack::None;
  };
  Snapshot snapshots_[QUEUE_DEPTH];
  size_t head_ = 0, count_ = 0;

  static bool keyIs(const char *token, size_t length, const char *key) {
    size_t i = 0;
    for (; key[i]; i++) {
      if (i >= length) return false;
      char c = token[i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != key[i]) return false;
    }
    return i < length && token[i] == '=';
  }

  static bool knownKey(const char *token, size_t length) {
    static const char *const keys[] = {
        "fw", "time", "tz", "wifi", "bat", "face", "faces", "bright", "motion", "format", "night",
        "proto", "caps", "mv", "temp", "drift", "timeq", "source", "syncage", "timer", "remain"};
    for (const char *key : keys) if (keyIs(token, length, key)) return true;
    return false;
  }

  static size_t tokenEnd(const Snapshot &s, size_t at) {
    const char *end = (const char *)memchr(s.text + at, ';', s.length - at);
    return end ? (size_t)(end - s.text) : s.length;
  }

  void pop() {
    head_ = (head_ + 1) % QUEUE_DEPTH;
    count_--;
  }
};

}  // namespace bleframes
