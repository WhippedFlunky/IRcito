#include "storage.h"
#include <Preferences.h>
#include <esp_partition.h>
#include <nvs.h>
#include <cstdio>
#include <cstring>

namespace store {
namespace {
constexpr uint32_t kMagic = 0x31524949; // IIR1, little endian
constexpr uint16_t kVersion = 1;
constexpr char kNamespace[] = "ircito";
// All persistent fields have explicit widths. Changing the layout requires a
// new version and a migration; old records are never parsed as new records.
struct __attribute__((packed)) Header {
  uint32_t magic;
  uint16_t version, bytes;
  uint32_t crc;
  uint32_t id;
  uint8_t remote, button, protocol, bits;
  uint32_t carrier, address, command;
  uint8_t repeats;
  uint32_t period;
  uint16_t rawCount;
  char name[20];
};
struct __attribute__((packed)) RemoteWire {
  uint32_t magic;
  uint16_t version, bytes;
  uint32_t crc, id;
  char name[20];
};
static_assert(sizeof(rmt_symbol_word_t) == 4, "RMT symbol format changed");
Preferences prefs;
bool ready = false;
uint8_t wire[sizeof(Header) + kMaxRaw * sizeof(rmt_symbol_word_t)];
Signal scan; // Internal RAM; avoids a large stack allocation in scan loops.

uint32_t crc32(const uint8_t *data, size_t n) {
  uint32_t crc = ~uint32_t(0);
  for (size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
void key(char out[8], char kind, uint8_t remote, uint8_t button = 0, char bank = 0) {
  out[0] = kind;
  out[1] = char('0' + remote);
  if (kind == 'r') out[2] = 0;
  else { out[2] = char('0' + button); out[3] = bank; out[4] = 0; }
}
bool validSlot(uint8_t remote, uint8_t button) { return remote < kRemotes && button < kButtons; }
bool validCarrier(uint32_t hz) {
  return hz == 36000 || hz == 38000 || hz == 40000;
}
bool validHeader(const Header &h, size_t n, uint8_t remote, uint8_t button) {
  if (h.magic != kMagic || h.version != kVersion || h.bytes != n ||
      h.remote != remote || h.button != button || h.rawCount > kMaxRaw ||
      n != sizeof(Header) + size_t(h.rawCount) * sizeof(rmt_symbol_word_t) ||
      !std::memchr(h.name, 0, sizeof(h.name))) return false;
  if (h.protocol == 0) {
    if (!validCarrier(h.carrier) || !h.rawCount || h.bits != 0) return false;
  } else if (h.protocol == 1) {
    if (h.carrier != 40000 || h.bits != 12 || h.address > 31 || h.command > 127 ||
        h.repeats != 3 || h.period != 45000) return false;
  } else if (h.protocol == 2) {
    if (h.carrier != 38000 || h.bits != 32 || h.address > 255 || h.command > 255 ||
        h.repeats != 1) return false;
  } else return false;
  if (h.repeats == 0 || h.repeats > 10 || h.period > 1000000) return false;
  return true;
}
bool read(uint8_t remote, uint8_t button, Signal &signal, bool quiet) {
  if (!ready || !validSlot(remote, button)) return false;
  char index[8]; key(index, 's', remote, button, 'i');
  const uint8_t bank = prefs.getUChar(index, 0xff);
  if (bank == 0xff) return false;
  if (bank > 1) { Serial.printf("[STORE] invalid index %s\n", index); return false; }
  char blob[8]; key(blob, 's', remote, button, bank ? 'b' : 'a');
  const size_t n = prefs.getBytesLength(blob);
  if (n < sizeof(Header) || n > sizeof(wire) || prefs.getBytes(blob, wire, n) != n) {
    Serial.printf("[STORE] invalid blob size/read %s (%u)\n", blob, unsigned(n));
    return false;
  }
  Header h;
  std::memcpy(&h, wire, sizeof(h));
  const uint32_t expected = h.crc;
  std::memset(wire + offsetof(Header, crc), 0, sizeof(h.crc));
  if (!validHeader(h, n, remote, button) || crc32(wire, n) != expected) {
    Serial.printf("[STORE] corrupt/incompatible %s; slot treated empty\n", blob);
    return false;
  }
  signal.id = h.id;
  std::memcpy(signal.name, h.name, sizeof(h.name));
  signal.decoded = h.protocol == 0 ? ir::DecodeResult{} :
    ir::DecodeResult{static_cast<ir::Protocol>(h.protocol), h.address, h.command, h.bits, true};
  signal.carrierHz = h.carrier;
  signal.repeatCount = h.repeats;
  signal.repeatPeriodUs = h.period;
  signal.rawCount = h.rawCount;
  if (h.rawCount) std::memcpy(signal.raw, wire + sizeof(Header), h.rawCount * 4u);
  if (!quiet) Serial.printf("[STORE] loaded remote=%u button=%u protocol=%u raw=%u\n",
                            remote + 1, button + 1, h.protocol, h.rawCount);
  return true;
}
size_t otherRawBytes(uint8_t remote, uint8_t button) {
  size_t total = 0;
  for (uint8_t r = 0; r < kRemotes; ++r)
    for (uint8_t b = 0; b < kButtons; ++b)
      if ((r != remote || b != button) && read(r, b, scan, true)) total += size_t(scan.rawCount) * 4;
  return total;
}
}

bool begin() {
  const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                        ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
  if (!part) { Serial.println("[STORE] no NVS partition named nvs; storage unavailable"); return false; }
  Serial.printf("[STORE] NVS partition=%u bytes (shared, not reformatted)\n", unsigned(part->size));
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[STORE] Preferences.begin failed; storage unavailable");
    return false;
  }
  ready = true;
  nvs_stats_t stats = {};
  if (nvs_get_stats("nvs", &stats) == ESP_OK)
    Serial.printf("[STORE] NVS entries free=%u used=%u; app RAW budget=%u bytes\n",
                  unsigned(stats.free_entries), unsigned(stats.used_entries), unsigned(kRawBudgetBytes));
  return true;
}
bool available() { return ready; }
bool loadRemote(uint8_t remote, Remote &record) {
  if (remote >= kRemotes) return false;
  record.id = remote + 1;
  std::snprintf(record.name, sizeof(record.name), "Remote %u", remote + 1);
  if (!ready) return false;
  char name[8]; key(name, 'r', remote);
  if (!prefs.isKey(name)) return true; // Default remote exists conceptually.
  RemoteWire h = {};
  if (prefs.getBytesLength(name) != sizeof(h) || prefs.getBytes(name, &h, sizeof(h)) != sizeof(h)) {
    Serial.printf("[STORE] corrupt remote %u; using default name\n", remote + 1); return false;
  }
  const auto crc = h.crc; h.crc = 0;
  if (h.magic != kMagic || h.version != kVersion || h.bytes != sizeof(h) ||
      h.id != uint32_t(remote + 1) || !std::memchr(h.name, 0, sizeof(h.name)) ||
      crc32(reinterpret_cast<const uint8_t *>(&h), sizeof(h)) != crc) {
    Serial.printf("[STORE] invalid remote %u; using default name\n", remote + 1); return false;
  }
  std::memcpy(record.name, h.name, sizeof(h.name));
  return true;
}
bool saveRemote(uint8_t remote, const Remote &record) {
  if (!ready || remote >= kRemotes || record.id != uint32_t(remote + 1) ||
      !std::memchr(record.name, 0, sizeof(record.name))) return false;
  RemoteWire h = {};
  h.magic = kMagic; h.version = kVersion; h.bytes = sizeof(h); h.id = record.id;
  std::memcpy(h.name, record.name, sizeof(h.name));
  h.crc = crc32(reinterpret_cast<const uint8_t *>(&h), sizeof(h));
  char name[8]; key(name, 'r', remote);
  return prefs.putBytes(name, &h, sizeof(h)) == sizeof(h);
}
bool loadSignal(uint8_t remote, uint8_t button, Signal &signal) { return read(remote, button, signal, false); }
bool saveSignal(uint8_t remote, uint8_t button, const Signal &signal) {
  if (!ready || !validSlot(remote, button) || signal.rawCount > kMaxRaw) return false;
  const uint8_t protocol = signal.decoded.valid ? uint8_t(signal.decoded.protocol) : 0;
  size_t rawCount = signal.rawCount;
  const size_t other = otherRawBytes(remote, button);
  if (other + rawCount * 4 > kRawBudgetBytes) {
    if (protocol == 0) {
      Serial.printf("[STORE] RAW budget exceeded: %u + %u > %u bytes\n",
                    unsigned(other), unsigned(rawCount * 4), unsigned(kRawBudgetBytes));
      return false;
    }
    rawCount = 0; // Canonical replay is sufficient; backup is optional.
    Serial.println("[STORE] optional recognized RAW backup omitted to fit budget");
  }
  Header h = {};
  h.magic = kMagic; h.version = kVersion;
  h.bytes = sizeof(h) + rawCount * 4;
  h.id = signal.id ? signal.id : uint32_t(remote * kButtons + button + 1);
  h.remote = remote; h.button = button; h.protocol = protocol;
  h.bits = protocol ? signal.decoded.bits : 0;
  h.carrier = signal.carrierHz;
  h.address = protocol ? signal.decoded.address : 0;
  h.command = protocol ? signal.decoded.command : 0;
  h.repeats = signal.repeatCount;
  h.period = signal.repeatPeriodUs;
  h.rawCount = rawCount;
  std::snprintf(h.name, sizeof(h.name), "Button %u", button + 1);
  if (!validHeader(h, h.bytes, remote, button)) {
    Serial.println("[STORE] refused invalid signal metadata"); return false;
  }
  std::memcpy(wire, &h, sizeof(h));
  if (rawCount) std::memcpy(wire + sizeof(h), signal.raw, rawCount * 4);
  h.crc = crc32(wire, h.bytes);
  std::memcpy(wire, &h, sizeof(h));
  char index[8]; key(index, 's', remote, button, 'i');
  const uint8_t oldBank = prefs.getUChar(index, 0xff);
  const uint8_t newBank = oldBank == 0 ? 1 : 0;
  char blob[8]; key(blob, 's', remote, button, newBank ? 'b' : 'a');
  if (prefs.putBytes(blob, wire, h.bytes) != h.bytes) {
    Serial.println("[STORE] NVS full/write failed; old slot retained"); return false;
  }
  if (prefs.getBytesLength(blob) != h.bytes || prefs.getBytes(blob, wire, h.bytes) != h.bytes) {
    Serial.println("[STORE] verification read failed; old slot retained"); return false;
  }
  Header check;
  std::memcpy(&check, wire, sizeof(check));
  const uint32_t checkCrc = check.crc;
  std::memset(wire + offsetof(Header, crc), 0, sizeof(check.crc));
  if (!validHeader(check, h.bytes, remote, button) || crc32(wire, h.bytes) != checkCrc) {
    Serial.println("[STORE] verification CRC failed; old slot retained"); return false;
  }
  if (prefs.putUChar(index, newBank) != 1) {
    Serial.println("[STORE] NVS full/index failed; old slot retained"); return false;
  }
  if (oldBank <= 1) {
    char previous[8]; key(previous, 's', remote, button, oldBank ? 'b' : 'a');
    if (!prefs.remove(previous)) Serial.println("[STORE] old blob cleanup deferred");
  }
  Serial.printf("[STORE] saved remote=%u button=%u protocol=%u raw=%u\n",
                remote + 1, button + 1, protocol, unsigned(rawCount));
  return true;
}
bool deleteSignal(uint8_t remote, uint8_t button) {
  if (!ready || !validSlot(remote, button)) return false;
  char index[8]; key(index, 's', remote, button, 'i');
  if (!prefs.isKey(index)) return true;
  if (!prefs.remove(index)) { Serial.println("[STORE] delete failed; slot retained"); return false; }
  for (char b : {'a','b'}) {
    char name[8]; key(name, 's', remote, button, b);
    if (prefs.isKey(name) && !prefs.remove(name)) Serial.printf("[STORE] cleanup failed: %s\n", name);
  }
  Serial.printf("[STORE] deleted remote=%u button=%u\n", remote + 1, button + 1);
  return true;
}
bool eraseAll() {
  if (!ready) return false;
  if (!prefs.clear()) { Serial.println("[STORE] erase namespace failed"); return false; }
  Serial.println("[STORE] erased only ircito namespace");
  return true;
}
} // namespace store
