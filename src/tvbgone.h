#pragma once
#include <stddef.h>
#include <stdint.h>
#include <driver/rmt_types.h>

namespace tvbgone {
struct Code {
  uint32_t hz;
  uint8_t pairs, bits, timingPairs;
  const uint16_t *times; // Original database uses units of 10 microseconds.
  const uint8_t *indices; // Original packed indices, MSB first.
};
enum class Region : uint8_t { NorthAmerica, Europe };
extern const Code kCodes[];
extern const uint16_t kNA[], kEU[];
extern const size_t kNACount, kEUCount;
size_t count(Region region);
const char *name(Region region);
const Code *code(Region region, size_t index);
// Caller owns static RMT workspace. Returns 0 on malformed/oversized codes.
size_t encode(const Code &code, rmt_symbol_word_t *output, size_t capacity);
} // namespace tvbgone
