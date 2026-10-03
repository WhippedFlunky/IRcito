#include <cassert>
#include <cstdio>
#include <cstring>
#include <Preferences.h>
#include "../src/macros.h"

int main() {
  assert(store::begin() && macros::begin());
  macros::Macro m, loaded;
  assert(!macros::load(0, m) && m.id == 1 && std::strcmp(m.name,"Macro 1") == 0);
  m.stepCount=2;
  m.steps[0]={0,0,1000}; m.steps[1]={1,2,500};
  assert(macros::valid(m,0) && macros::save(0,m));
  assert(macros::load(0,loaded) && loaded.stepCount==2 && loaded.steps[1].button==2);
  // A missing signal is detected before starting/when reaching that step.
  store::Signal signal;
  assert(!store::loadSignal(1,2,signal));
  m.stepCount=9; assert(!macros::save(0,m)); m.stepCount=2;
  m.steps[1].delayAfterMs=999; assert(!macros::save(0,m)); m.steps[1].delayAfterMs=500;
  std::strcpy(m.name,"TV Sequence");
  Preferences::failBlob=true; assert(!macros::save(0,m)); Preferences::failBlob=false;
  assert(macros::load(0,loaded) && std::strcmp(loaded.name,"Macro 1")==0);
  Preferences::failIndex=true; assert(!macros::save(0,m)); Preferences::failIndex=false;
  assert(macros::load(0,loaded) && std::strcmp(loaded.name,"Macro 1")==0);
  assert(macros::save(0,m) && macros::load(0,loaded) && std::strcmp(loaded.name,"TV Sequence")==0);
  auto &active=Preferences::values["ircito/m0b"];
  assert(!active.empty()); active.back() ^= 1;
  assert(!macros::load(0,loaded));
  assert(macros::remove(0) && !macros::load(0,loaded));
  std::puts("PASS: macro save/load/delete, missing reference, bounds, delays, corruption, transactional overwrite");
}
