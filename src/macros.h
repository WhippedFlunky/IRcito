#pragma once
#include <stdint.h>
#include "storage.h"

namespace macros {
constexpr uint8_t kSlots = 4;
constexpr uint8_t kMaxSteps = 8;
struct Step {
  uint8_t remote = 0, button = 0; // Zero-based storage slots.
  uint16_t delayAfterMs = 0;
};
struct Macro {
  uint32_t id = 0; // One-based ID.
  char name[20] = {};
  uint8_t stepCount = 0;
  Step steps[kMaxSteps] = {};
};
bool valid(const Macro &macro, uint8_t slot);
bool begin();
void defaults(uint8_t slot, Macro &out);
bool load(uint8_t slot, Macro &out); // Returns false for empty/corrupt; out gets defaults.
bool save(uint8_t slot, const Macro &macro);
bool remove(uint8_t slot);
} // namespace macros
