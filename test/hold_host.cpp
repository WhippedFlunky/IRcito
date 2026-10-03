#include <cassert>
#include <cstdio>
#include "../src/hold_policy.h"
int main() {
  store::Signal s;
  assert(hold::forSignal(s).mode == hold::Mode::None);
  s.decoded = {ir::Protocol::SonySIRC12,1,18,12,true};
  assert(hold::forSignal(s).periodUs == 45000);
  assert(hold::firstRepeatOffsetUs(hold::forSignal(s)) == 135000);
  s.decoded = {ir::Protocol::NEC,1,18,32,true};
  assert(hold::forSignal(s).mode == hold::Mode::NecRepeat && hold::forSignal(s).periodUs == 110000);
  assert(hold::firstRepeatOffsetUs(hold::forSignal(s)) == 110000);
  s.decoded = {}; s.rawCount=1; s.repeatCount=5; s.repeatPeriodUs=100000;
  assert(hold::forSignal(s).mode == hold::Mode::RawFrame && hold::forSignal(s).maxAdditional == 4);
  s.repeatPeriodUs=1000; assert(hold::forSignal(s).mode == hold::Mode::None);
  std::puts("PASS: Sony 45ms, NEC repeat 110ms, explicit RAW policy, legacy RAW no repeat");
}
