#include <cassert>
#include <cstdio>
#include <cstring>
#include <Preferences.h>
#include "../src/storage.h"

int main() {
  assert(store::begin() && store::available());
  store::Remote remote;
  assert(store::loadRemote(0, remote) && std::strcmp(remote.name, "Remote 1") == 0);
  assert(store::saveRemote(0, remote));
  store::Signal raw, loaded;
  raw.carrierHz=36000; raw.rawCount=59;
  for(size_t i=0;i<raw.rawCount;++i) {
    raw.raw[i].duration0=500+i; raw.raw[i].level0=0;
    raw.raw[i].duration1=600+i; raw.raw[i].level1=1;
  }
  assert(store::saveSignal(0,0,raw));
  assert(store::loadSignal(0,0,loaded));
  assert(!loaded.decoded.valid && loaded.carrierHz==36000 && loaded.rawCount==59);
  for(size_t i=0;i<59;++i) assert(loaded.raw[i].val==raw.raw[i].val);
  raw.carrierHz=40000;
  Preferences::failBlob=true;
  assert(!store::saveSignal(0,0,raw));
  Preferences::failBlob=false;
  assert(store::loadSignal(0,0,loaded) && loaded.carrierHz==36000);
  Preferences::failIndex=true;
  assert(!store::saveSignal(0,0,raw));
  Preferences::failIndex=false;
  assert(store::loadSignal(0,0,loaded) && loaded.carrierHz==36000);
  assert(store::saveSignal(0,0,raw));
  assert(store::loadSignal(0,0,loaded) && loaded.carrierHz==40000);
  assert(store::deleteSignal(0,0));
  assert(store::begin() && !store::loadSignal(0,0,loaded)); // Simulated reboot.
  store::Signal sony;
  sony.decoded={ir::Protocol::SonySIRC12,1,18,12,true};
  sony.carrierHz=40000; sony.repeatCount=3; sony.repeatPeriodUs=45000;
  assert(store::saveSignal(0,1,sony));
  assert(store::loadSignal(0,1,loaded));
  assert(loaded.decoded.valid && loaded.decoded.command==18 && loaded.decoded.address==1);
  // Corruption never loads a signal into replay.
  auto &bytes=Preferences::values["ircito/s01a"];
  assert(!bytes.empty()); bytes.back() ^= 1;
  assert(!store::loadSignal(0,1,loaded));
  store::Signal large;
  large.rawCount=255;
  for(int i=0;i<4;++i) assert(store::saveSignal(1,i,large));
  large.rawCount=5;
  assert(!store::saveSignal(2,0,large)); // Explicit RAW quota, no silent truncation.
  sony.rawCount=59;
  assert(store::saveSignal(2,0,sony)); // Canonical data fits; optional RAW omitted.
  assert(store::loadSignal(2,0,loaded) && loaded.rawCount==0 && loaded.decoded.command==18);
  // NVS namespace separation.
  Preferences::values["launcher/token"]={1,2,3};
  assert(store::eraseAll() && Preferences::values["launcher/token"].size()==3);
  assert(!store::loadSignal(0,1,loaded));
  std::puts("PASS: RAW persistence, reboot, transactional overwrite failures, Sony metadata, quota, corruption, delete, namespace erase");
}
