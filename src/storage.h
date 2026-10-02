#pragma once
#include <Arduino.h>
#include <driver/rmt_types.h>
#include "ir_pipeline.h"

namespace store {
constexpr uint8_t kRemotes = 4;
constexpr uint8_t kButtons = 4;
constexpr size_t kMaxRaw = 256;
// Shared NVS is normally 20 KiB in the build partition CSV. The booted
// EasyLauncher layout is inspected at runtime; writes can still fail safely.
constexpr size_t kRawBudgetBytes = 4096;

struct Remote { uint32_t id; char name[20]; };
struct Signal {
  uint32_t id = 0;
  char name[20] = {};
  ir::DecodeResult decoded;
  uint32_t carrierHz = 38000;
  uint8_t repeatCount = 1;
  uint32_t repeatPeriodUs = 0;
  uint16_t rawCount = 0;
  rmt_symbol_word_t raw[kMaxRaw] = {};
};

bool begin();
bool available();
bool saveRemote(uint8_t remote, const Remote &record);
bool loadRemote(uint8_t remote, Remote &record);
bool saveSignal(uint8_t remote, uint8_t button, const Signal &signal);
bool loadSignal(uint8_t remote, uint8_t button, Signal &signal);
bool deleteSignal(uint8_t remote, uint8_t button);
bool eraseAll(); // Only this app's NVS namespace.
} // namespace store
