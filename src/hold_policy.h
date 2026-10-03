#pragma once
#include "storage.h"

namespace hold {
enum class Mode : uint8_t { None, SonyFrame, NecRepeat, RawFrame };
struct Policy {
  Mode mode = Mode::None;
  uint32_t periodUs = 0;
  uint8_t maxAdditional = 0; // 0 means while pressed for recognized protocols.
};
inline Policy forSignal(const store::Signal &s) {
  if (s.decoded.valid && s.decoded.protocol == ir::Protocol::SonySIRC12)
    return {Mode::SonyFrame, 45000, 0};
  if (s.decoded.valid && s.decoded.protocol == ir::Protocol::NEC)
    return {Mode::NecRepeat, 110000, 0};
  // The original v1.4 RAW records have repeatCount=1 and period=0: no repeats.
  if (!s.decoded.valid && s.rawCount && s.repeatCount > 1 && s.repeatCount <= 10 &&
      s.repeatPeriodUs >= 50000 && s.repeatPeriodUs <= 1000000)
    return {Mode::RawFrame, s.repeatPeriodUs, uint8_t(s.repeatCount - 1)};
  return {};
}
inline uint32_t firstRepeatOffsetUs(Policy p) {
  // Normal Sony Test already sent three frames (0/45/90 ms).
  return p.mode == Mode::SonyFrame ? 3 * p.periodUs : p.periodUs;
}
} // namespace hold
