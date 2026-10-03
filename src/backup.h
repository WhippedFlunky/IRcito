#pragma once
#include <stddef.h>
#include <stdint.h>

namespace backup {
// Versioned, one-record-per-line stream. A record is validated before its
// destination slot is committed; a truncated import cannot clear other slots.
using Write = void (*)(const char *line, void *context);
bool exportAll(Write write, void *context);
class Importer {
 public:
  void reset();
  bool feed(char c); // false on invalid/oversize record; later records still accepted.
  bool complete() const { return finished_ && started_; }
  unsigned imported() const { return imported_; }
  unsigned errors() const { return errors_; }
 private:
  bool process();
  char line_[2400] = {};
  size_t length_ = 0;
  bool started_ = false, finished_ = false;
  unsigned imported_ = 0, errors_ = 0;
};
} // namespace backup
