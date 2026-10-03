#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <Preferences.h>
#include "../src/backup.h"
#include "../src/storage.h"
#include "../src/macros.h"

static std::vector<std::string> lines;
static void collect(const char *line, void *) {lines.emplace_back(line);}
static uint32_t crc(const std::string &s) {
  uint32_t c=~uint32_t(0);
  for (unsigned char x:s) {
    c^=x;
    for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&(0u-(c&1)));
  }
  return ~c;
}
static std::string wrap(std::string payload) {
  char trailer[12];std::snprintf(trailer,sizeof(trailer),"|%08X",crc(payload));
  return payload+trailer;
}
static bool feed(backup::Importer &i,const std::string &line) {
  bool okay=true;
  for(char c:line) okay=i.feed(c)&&okay;
  return i.feed('\n')&&okay;
}
int main() {
  assert(store::begin() && macros::begin());
  store::Remote remote{1,"Sony TV"};assert(store::saveRemote(0,remote));
  store::Signal sony;
  std::strcpy(sony.name,"Volume +");
  sony.decoded={ir::Protocol::SonySIRC12,1,18,12,true};
  sony.carrierHz=40000;sony.repeatCount=3;sony.repeatPeriodUs=45000;
  assert(store::saveSignal(0,0,sony));
  store::Signal raw;
  std::strcpy(raw.name,"Unknown");raw.carrierHz=36000;raw.rawCount=3;
  for(unsigned j=0;j<3;++j)raw.raw[j].val=0x12345678u+j;
  assert(store::saveSignal(1,2,raw));
  macros::Macro m;macros::defaults(0,m);
  std::strcpy(m.name,"Evening");m.stepCount=2;
  m.steps[0]={0,0,1000};m.steps[1]={1,2,500};
  assert(macros::save(0,m));
  assert(backup::exportAll(collect,nullptr));
  assert(lines.front()=="IRCITO-BACKUP|1" && lines.back()=="END");
  // Reboot into an empty app namespace, retaining an unrelated namespace.
  Preferences::values["launcher/token"]={1};
  assert(store::eraseAll());
  backup::Importer importer;importer.reset();
  for(const auto &line:lines) assert(feed(importer,line));
  assert(importer.complete() && !importer.errors());
  assert(Preferences::values["launcher/token"].size()==1);
  store::Remote loadedRemote;store::Signal loaded;
  assert(store::loadRemote(0,loadedRemote) && std::strcmp(loadedRemote.name,"Sony TV")==0);
  assert(store::loadSignal(0,0,loaded) && loaded.decoded.command==18 &&
         loaded.repeatCount==3 && std::strcmp(loaded.name,"Volume +")==0);
  assert(store::loadSignal(1,2,loaded) && loaded.carrierHz==36000 &&
         loaded.rawCount==3 && loaded.raw[1].val==0x12345679u);
  assert(macros::load(0,m) && m.stepCount==2 && std::strcmp(m.name,"Evening")==0);
  // Truncation cannot erase records already stored or unvisited slots.
  backup::Importer partial;partial.reset();
  assert(feed(partial,"IRCITO-BACKUP|1"));
  assert(feed(partial,lines[1]));
  assert(!partial.complete() && store::loadSignal(1,2,loaded));
  backup::Importer invalid;invalid.reset();
  assert(!feed(invalid,"IRCITO-BACKUP|2"));
  assert(!feed(invalid,lines[1]));
  // Invalid records in an otherwise valid stream must leave old slots intact.
  auto reject=[&](const std::string &record) {
    backup::Importer next;next.reset();
    assert(feed(next,"IRCITO-BACKUP|1"));
    assert(!feed(next,wrap(record)));
    assert(store::loadSignal(1,2,loaded) && loaded.carrierHz==36000);
  };
  reject("S|4|2|556E6B6E6F776E|0|36000|0|0|0|1|0|3|12345678123456791234567A");
  reject("S|1|2|556E6B6E6F776E|0|41000|0|0|0|1|0|3|12345678123456791234567A");
  reject("S|1|2|556E6B6E6F776E|3|36000|0|0|0|1|0|3|12345678123456791234567A");
  reject("S|1|2|556E6B6E6F776E|0|36000|0|0|0|1|0|257|");
  reject("M|4|4D6163726F|1|0,0,1000;");
  reject("M|0|4D6163726F|9|");
  std::puts("PASS: backup round-trip, names, canonical/RAW, macros, truncation, version, bounds, carrier, namespace");
}
