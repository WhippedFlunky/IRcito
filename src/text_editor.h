#pragma once
#include <stdint.h>

namespace editor {
// A accepts the selected character, B cycles it. Hold A saves, hold B cancels.
// The first candidate is '-' (backspace) when editing an existing name.
struct State {
  char text[20] = {};
  uint8_t cursor = 0, candidate = 0;
  void start(const char *original);
  void next();
  void accept();
  char current() const;
};
} // namespace editor
