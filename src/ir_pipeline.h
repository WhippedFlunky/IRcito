#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ir {

struct Run {
  bool level;
  uint32_t durationUs;
};

struct Merge {
  bool level;
  uint32_t firstUs;
  uint32_t glitchUs;
  uint32_t lastUs;
  uint32_t totalUs;
};

enum class Protocol : uint8_t { Unknown, SonySIRC12, NEC };

struct DecodeResult {
  Protocol protocol = Protocol::Unknown;
  uint32_t address = 0;
  uint32_t command = 0;
  uint16_t bits = 0;
  bool valid = false;
};

template <typename Symbol>
bool normalize(const Symbol *symbols, size_t symbolCount, Run *runs,
               size_t capacity, size_t &runCount) {
  runCount = 0;
  if (!symbols || !runs) return false;
  for (size_t i = 0; i < symbolCount; ++i) {
    const uint32_t durations[2] = {symbols[i].duration0, symbols[i].duration1};
    const bool levels[2] = {bool(symbols[i].level0), bool(symbols[i].level1)};
    for (unsigned half = 0; half < 2; ++half) {
      if (!durations[half]) continue;
      if (runCount && runs[runCount - 1].level == levels[half]) {
        if (runs[runCount - 1].durationUs > UINT32_MAX - durations[half]) return false;
        runs[runCount - 1].durationUs += durations[half];
      } else {
        if (runCount >= capacity) return false;
        runs[runCount++] = {levels[half], durations[half]};
      }
    }
  }
  return true;
}

// One forward pass. Merged runs are not reconsidered; shared/ambiguous triples
// remain unchanged. These are protocol-independent physical run constraints.
inline size_t deglitch(Run *runs, size_t &count, Merge *logs, size_t logCapacity) {
  size_t merged = 0;
  for (size_t i = 1; i + 1 < count;) {
    const Run a = runs[i - 1], gap = runs[i], b = runs[i + 1];
    const uint32_t total = a.durationUs + gap.durationUs + b.durationUs;
    const uint32_t smaller = a.durationUs < b.durationUs ? a.durationUs : b.durationUs;
    const uint32_t larger = a.durationUs > b.durationUs ? a.durationUs : b.durationUs;
    const bool safe = a.level == b.level && a.level != gap.level &&
        a.durationUs >= 80 && b.durationUs >= 80 && larger <= 450 &&
        larger <= smaller * 2 && gap.durationUs >= 50 &&
        gap.durationUs <= 320 && total >= 350 && total <= 2000;
    if (!safe) { ++i; continue; }
    if (logs && merged < logCapacity) {
      logs[merged] = {a.level, a.durationUs, gap.durationUs, b.durationUs, total};
    }
    runs[i - 1].durationUs = total;
    for (size_t j = i; j + 2 < count; ++j) runs[j] = runs[j + 2];
    count -= 2;
    ++merged;
    i += 2;
  }
  return merged;
}

inline bool decodeSony(const Run *runs, size_t count, DecodeResult &result) {
  bool found = false;
  uint16_t decoded = 0;
  for (size_t start = 0; start <= 8 && start + 23 <= count; ++start) {
    const size_t remaining = count - start;
    if (remaining != 23 && remaining != 24) continue;
    bool prefixValid = true;
    for (size_t i = 0; i < start; ++i) {
      if (!runs[i].level && runs[i].durationUs >= 240 &&
          runs[i].durationUs <= 1350) prefixValid = false;
    }
    if (!prefixValid) continue;

    uint32_t marks[12] = {};
    uint32_t minSpace = UINT32_MAX, maxSpace = 0;
    bool valid = true;
    for (size_t bit = 0; bit < 12; ++bit) {
      const Run &mark = runs[start + bit * 2];
      if (mark.level || mark.durationUs < 240 || mark.durationUs > 1350) {
        valid = false;
        break;
      }
      marks[bit] = mark.durationUs;
      const size_t spaceIndex = start + bit * 2 + 1;
      if (spaceIndex < count) {
        const Run &space = runs[spaceIndex];
        if (!space.level || space.durationUs < 400 || space.durationUs > 1400) {
          valid = false;
          break;
        }
        if (space.durationUs < minSpace) minSpace = space.durationUs;
        if (space.durationUs > maxSpace) maxSpace = space.durationUs;
      }
    }
    if (!valid || maxSpace > minSpace * 2) continue;

    uint32_t sorted[12] = {};
    for (size_t i = 0; i < 12; ++i) {
      size_t j = i;
      while (j && sorted[j - 1] > marks[i]) {
        sorted[j] = sorted[j - 1];
        --j;
      }
      sorted[j] = marks[i];
    }
    unsigned splits = 0;
    uint32_t threshold = 0;
    for (size_t split = 2; split <= 10; ++split) {
      const uint32_t shortMin = sorted[0], shortMax = sorted[split - 1];
      const uint32_t longMin = sorted[split], longMax = sorted[11];
      if (shortMax > 720 || longMin < 750 || longMax > 1350 ||
          shortMax * 100 > shortMin * 180 || longMax * 100 > longMin * 160 ||
          longMin * 100 < shortMax * 130) continue;
      ++splits;
      threshold = (shortMax + longMin) / 2;
    }
    if (splits != 1) continue;
    uint16_t payload = 0;
    for (size_t bit = 0; bit < 12; ++bit) {
      if (marks[bit] > threshold) payload |= uint16_t(1u << bit);
    }
    if (found) return false;
    found = true;
    decoded = payload;
  }
  if (!found) return false;
  result = {Protocol::SonySIRC12, uint32_t((decoded >> 7) & 0x1f),
            uint32_t(decoded & 0x7f), 12, true};
  return true;
}

inline bool near(uint32_t value, uint32_t minimum, uint32_t maximum) {
  return value >= minimum && value <= maximum;
}

inline bool decodeNec(const Run *runs, size_t count, DecodeResult &result) {
  bool found = false;
  uint8_t address = 0, command = 0;
  for (size_t start = 0; start <= 6 && start + 67 <= count; ++start) {
    if (count - start != 67 && count - start != 68) continue;
    bool valid = true;
    for (size_t i = 0; i < start; ++i) {
      if (runs[i].durationUs > 350) valid = false;
    }
    if (!valid || runs[start].level || !near(runs[start].durationUs, 8000, 10000) ||
        !runs[start + 1].level || !near(runs[start + 1].durationUs, 3800, 5200)) continue;
    uint32_t payload = 0;
    for (size_t bit = 0; bit < 32; ++bit) {
      const Run &mark = runs[start + 2 + 2 * bit];
      const Run &space = runs[start + 3 + 2 * bit];
      if (mark.level || !near(mark.durationUs, 350, 750) || !space.level) {
        valid = false;
        break;
      }
      if (near(space.durationUs, 350, 800)) {
        // Logical 0.
      } else if (near(space.durationUs, 1300, 1950)) {
        payload |= uint32_t(1) << bit;
      } else {
        valid = false;
        break;
      }
    }
    if (!valid || runs[start + 66].level ||
        !near(runs[start + 66].durationUs, 350, 750)) continue;
    if (count - start == 68 && (!runs[start + 67].level ||
        !near(runs[start + 67].durationUs, 350, 1200))) continue;
    const uint8_t a = uint8_t(payload), aInv = uint8_t(payload >> 8);
    const uint8_t c = uint8_t(payload >> 16), cInv = uint8_t(payload >> 24);
    if (uint8_t(a ^ aInv) != 0xff || uint8_t(c ^ cInv) != 0xff) continue;
    if (found) return false;
    found = true;
    address = a;
    command = c;
  }
  if (!found) return false;
  result = {Protocol::NEC, address, command, 32, true};
  return true;
}

inline DecodeResult decode(const Run *runs, size_t count) {
  DecodeResult sony, nec;
  const bool hasSony = decodeSony(runs, count, sony);
  const bool hasNec = decodeNec(runs, count, nec);
  if (hasSony == hasNec) return {}; // No match or ambiguous match.
  return hasSony ? sony : nec;
}

} // namespace ir
