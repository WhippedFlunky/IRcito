#include "tvbgone.h"
#include <cstring>

namespace tvbgone {
size_t count(Region region) { return region == Region::NorthAmerica ? kNACount : kEUCount; }
const char *name(Region region) { return region == Region::NorthAmerica ? "North America" : "Europe"; }
const Code *code(Region region, size_t index) {
  if (index >= count(region)) return nullptr;
  return &kCodes[region == Region::NorthAmerica ? kNA[index] : kEU[index]];
}
size_t encode(const Code &entry, rmt_symbol_word_t *output, size_t capacity) {
  if (!output || capacity == 0 || !entry.times || !entry.indices ||
      entry.pairs == 0 || entry.bits < 2 || entry.bits > 3 ||
      entry.timingPairs == 0 || entry.timingPairs > (1u << entry.bits)) return 0;
  std::memset(output, 0, capacity * sizeof(*output));
  size_t halves = 0;
  auto append = [&](bool level, uint32_t us) -> bool {
    while (us) {
      const uint16_t chunk = us > 32767 ? 32767 : uint16_t(us);
      if (halves >= capacity * 2) return false;
      rmt_symbol_word_t &sym = output[halves / 2];
      if (halves & 1) { sym.level1 = level; sym.duration1 = chunk; }
      else { sym.level0 = level; sym.duration0 = chunk; }
      ++halves;
      us -= chunk;
    }
    return true;
  };
  for (size_t pair = 0; pair < entry.pairs; ++pair) {
    unsigned selected = 0;
    for (unsigned bit = 0; bit < entry.bits; ++bit) {
      const size_t offset = pair * entry.bits + bit;
      selected = (selected << 1) | ((entry.indices[offset / 8] >> (7 - offset % 8)) & 1u);
    }
    if (selected >= entry.timingPairs) return 0;
    if (!append(true, uint32_t(entry.times[2 * selected]) * 10) ||
        !append(false, uint32_t(entry.times[2 * selected + 1]) * 10)) return 0;
  }
  // Only the very last half may be 0-duration; RMT uses 0 to end a symbol.
  return (halves + 1) / 2;
}
} // namespace tvbgone
