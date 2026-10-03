#include "macros.h"
#include <Preferences.h>
#include <cstdio>
#include <cstring>

namespace macros {
namespace {
constexpr uint32_t kMagic = 0x3143414d; // MAC1
constexpr uint16_t kVersion = 1;
struct __attribute__((packed)) WireStep { uint8_t remote, button; uint16_t delay; };
struct __attribute__((packed)) Wire {
  uint32_t magic;
  uint16_t version, bytes;
  uint32_t crc, id;
  uint8_t count;
  char name[20];
  WireStep steps[kMaxSteps];
};
Preferences prefs;
bool ready = false;
uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = ~uint32_t(0);
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return ~c;
}
void key(char out[5], uint8_t slot, char bank) {
  out[0]='m'; out[1]=char('0'+slot); out[2]=bank; out[3]=0;
}
bool validDelay(uint16_t ms) {
  return ms == 0 || ms == 250 || ms == 500 || ms == 1000 || ms == 2000;
}
}
bool valid(const Macro &m, uint8_t slot) {
  if (slot >= kSlots || m.id != uint32_t(slot + 1) ||
      !store::validName(m.name) || m.stepCount > kMaxSteps) return false;
  for (uint8_t i = 0; i < m.stepCount; ++i)
    if (m.steps[i].remote >= store::kRemotes || m.steps[i].button >= store::kButtons ||
        !validDelay(m.steps[i].delayAfterMs)) return false;
  return true;
}
bool begin() {
  ready = store::available() && prefs.begin("ircito", false);
  if (!ready) Serial.println("[MACRO] storage unavailable");
  return ready;
}
void defaults(uint8_t slot, Macro &out) {
  out = {};
  if (slot < kSlots) {
    out.id = slot + 1;
    std::snprintf(out.name, sizeof(out.name), "Macro %u", slot + 1);
  }
}
bool load(uint8_t slot, Macro &out) {
  defaults(slot, out);
  if (!ready || slot >= kSlots) return false;
  char index[5]; key(index, slot, 'i');
  const uint8_t bank = prefs.getUChar(index, 0xff);
  if (bank == 0xff) return false;
  if (bank > 1) { Serial.printf("[MACRO] invalid index %u\n", slot + 1); return false; }
  char blob[5]; key(blob, slot, bank ? 'b' : 'a');
  Wire w = {};
  if (prefs.getBytesLength(blob) != sizeof(w) || prefs.getBytes(blob, &w, sizeof(w)) != sizeof(w)) {
    Serial.printf("[MACRO] invalid size/read %u\n", slot + 1); return false;
  }
  const uint32_t expected = w.crc; w.crc = 0;
  Macro candidate;
  candidate.id = w.id;
  std::memcpy(candidate.name, w.name, 20);
  candidate.stepCount = w.count;
  for (uint8_t i = 0; i < kMaxSteps; ++i) {
    candidate.steps[i].remote = w.steps[i].remote;
    candidate.steps[i].button = w.steps[i].button;
    candidate.steps[i].delayAfterMs = w.steps[i].delay;
  }
  if (w.magic != kMagic || w.version != kVersion || w.bytes != sizeof(w) ||
      crc32(reinterpret_cast<const uint8_t *>(&w), sizeof(w)) != expected ||
      !valid(candidate, slot)) {
    Serial.printf("[MACRO] corrupt/incompatible %u\n", slot + 1); return false;
  }
  out = candidate;
  return true;
}
bool save(uint8_t slot, const Macro &m) {
  if (!ready || !valid(m, slot)) { Serial.println("[MACRO] refused invalid macro"); return false; }
  Wire w = {};
  w.magic = kMagic; w.version = kVersion; w.bytes = sizeof(w); w.id = m.id;
  w.count = m.stepCount; std::memcpy(w.name, m.name, 20);
  for (uint8_t i = 0; i < m.stepCount; ++i) {
    w.steps[i].remote = m.steps[i].remote;
    w.steps[i].button = m.steps[i].button;
    w.steps[i].delay = m.steps[i].delayAfterMs;
  }
  w.crc = crc32(reinterpret_cast<const uint8_t *>(&w), sizeof(w));
  char index[5]; key(index, slot, 'i');
  const uint8_t old = prefs.getUChar(index, 0xff);
  if (old != 0xff && old > 1) return false;
  const uint8_t next = old == 0 ? 1 : 0;
  char blob[5]; key(blob, slot, next ? 'b' : 'a');
  if (prefs.putBytes(blob, &w, sizeof(w)) != sizeof(w)) {
    Serial.println("[MACRO] NVS full/write failed; old macro retained"); return false;
  }
  Wire check = {};
  if (prefs.getBytesLength(blob) != sizeof(check) ||
      prefs.getBytes(blob, &check, sizeof(check)) != sizeof(check)) return false;
  const uint32_t crc = check.crc; check.crc = 0;
  if (crc32(reinterpret_cast<const uint8_t *>(&check), sizeof(check)) != crc ||
      crc != w.crc) return false;
  if (prefs.putUChar(index, next) != 1) {
    Serial.println("[MACRO] NVS full/index failed; old macro retained"); return false;
  }
  if (old <= 1) {
    char previous[5]; key(previous, slot, old ? 'b' : 'a');
    if (!prefs.remove(previous)) Serial.println("[MACRO] stale blob cleanup deferred");
  }
  Serial.printf("[MACRO] saved slot=%u steps=%u\n", slot + 1, m.stepCount);
  return true;
}
bool remove(uint8_t slot) {
  if (!ready || slot >= kSlots) return false;
  char index[5]; key(index, slot, 'i');
  if (prefs.isKey(index) && !prefs.remove(index)) return false;
  for (char c : {'a','b'}) {
    char blob[5]; key(blob, slot, c);
    if (prefs.isKey(blob) && !prefs.remove(blob)) Serial.printf("[MACRO] cleanup failed %s\n", blob);
  }
  Serial.printf("[MACRO] deleted slot=%u\n", slot + 1);
  return true;
}
} // namespace macros
