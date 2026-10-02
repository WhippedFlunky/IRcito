#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
struct Symbol { uint32_t duration0, level0, duration1, level1; };
#include "../src/ir_pipeline.h"
using Frame = std::vector<Symbol>;

ir::DecodeResult analyze(const Frame &frame, size_t &before, size_t &after, size_t &merges) {
  ir::Run runs[512] = {};
  assert(ir::normalize(frame.data(), frame.size(), runs, 512, before));
  after = before;
  ir::Merge details[8] = {};
  merges = ir::deglitch(runs, after, details, 8);
  return ir::decode(runs, after);
}
Frame sony(uint8_t command, uint8_t address) {
  Frame f = {{2377,0,922,1}};
  const unsigned data = command | (unsigned(address) << 7);
  for (unsigned i=0; i<12; ++i)
    f.push_back({(data & (1u<<i)) ? 940u:390u,0,i==11?0u:810u,1});
  return f;
}
Frame nec(uint8_t address, uint8_t command) {
  Frame f = {{9000,0,4500,1}};
  const uint32_t data = uint32_t(address) | (uint32_t(uint8_t(~address))<<8) |
      (uint32_t(command)<<16) | (uint32_t(uint8_t(~command))<<24);
  for (unsigned i=0;i<32;++i)
    f.push_back({560,0,(data & (uint32_t(1)<<i)) ? 1690u:560u,1});
  f.push_back({560,0,0,1});
  return f;
}
int main() {
  size_t before=0,after=0,merged=0;
  auto f = sony(18,1);
  auto r = analyze(f,before,after,merged);
  assert(r.valid && r.protocol==ir::Protocol::SonySIRC12 && r.command==18 && r.address==1);
  f=sony(21,1);
  r=analyze(f,before,after,merged);
  assert(r.valid && r.protocol==ir::Protocol::SonySIRC12 && r.command==21 && r.address==1);
  Frame splitVol={
    {1427,0,600,1},{599,0,628,1},{1172,0,630,1},{572,0,631,1},
    {173,0,153,1},{246,0,629,1},{1171,0,631,1},{572,0,628,1},
    {575,0,828,1},{977,0,774,1},{424,0,627,1},{572,0,606,1},
    {146,0,307,1},{144,0,630,1},{573,0,0,1}};
  auto original=splitVol;
  r=analyze(splitVol,before,after,merged);
  assert(r.valid && r.protocol==ir::Protocol::SonySIRC12 && r.command==18 && r.address==1);
  assert(merged==2 && before==29 && after==25);
  Frame croppedVol={
    {122,0,529,1},{1499,0,649,1},{572,0,634,1},{1176,0,642,1},
    {575,0,631,1},{573,0,632,1},{1173,0,646,1},{575,0,630,1},
    {574,0,632,1},{1173,0,644,1},{573,0,633,1},{575,0,630,1},
    {574,0,632,1},{575,0,0,1}};
  r=analyze(croppedVol,before,after,merged);
  assert(r.valid && r.protocol==ir::Protocol::SonySIRC12 && r.command==18 && r.address==1);
  Frame recordedPower={
    {2377,0,922,1},{856,0,862,1},{354,0,802,1},{1006,0,860,1},
    {352,0,804,1},{1004,0,863,1},{353,0,802,1},{401,0,804,1},
    {1004,0,863,1},{353,0,777,1},{482,0,748,1},{403,0,802,1},
    {405,0,0,1}};
  r=analyze(recordedPower,before,after,merged);
  assert(r.valid && r.protocol==ir::Protocol::SonySIRC12 && r.command==21 && r.address==1);
  for(size_t i=0;i<original.size();++i)
    assert(original[i].duration0==splitVol[i].duration0 && original[i].duration1==splitVol[i].duration1 &&
           original[i].level0==splitVol[i].level0 && original[i].level1==splitVol[i].level1);
  Frame base=nec(0x34,0x12);
  base[2].duration0=547;
  base[5].duration0=546;
  Frame one=base, two=base;
  one[2]={173,0,248,1}; one.insert(one.begin()+3,{126,0,base[2].duration1,1});
  two[5]={199,0,222,1}; two.insert(two.begin()+6,{125,0,base[5].duration1,1});
  auto both=one;
  both[6]={199,0,222,1}; both.insert(both.begin()+7,{125,0,base[5].duration1,1});
  const Frame variants[]={base,one,two,both};
  size_t reference=0;
  ir::Run expectedRuns[512] = {};
  for(const auto &v: variants) {
    ir::Run runs[512] = {};
    assert(ir::normalize(v.data(),v.size(),runs,512,before));
    after=before;
    merged=ir::deglitch(runs,after,nullptr,0);
    r=ir::decode(runs,after);
    assert(r.valid && r.protocol==ir::Protocol::NEC && r.address==0x34 && r.command==0x12);
    if (!reference) {
      reference=after;
      for(size_t j=0;j<after;++j) expectedRuns[j]=runs[j];
    }
    assert(after==reference);
    for(size_t j=0;j<after;++j)
      assert(runs[j].level==expectedRuns[j].level && runs[j].durationUs==expectedRuns[j].durationUs);
  }
  assert(variants[3].size()==variants[0].size()+2);
  f=nec(0x34,0x12); f[9].duration1=999; // neither NEC zero nor one
  r=analyze(f,before,after,merged); assert(!r.valid);
  f=nec(0x34,0x12); f[1].duration1=1690; // valid timing, invalid complements
  r=analyze(f,before,after,merged); assert(!r.valid);
  ir::Run trap[]={{true,570},{false,147},{true,177}};
  size_t count=3;
  assert(ir::deglitch(trap,count,nullptr,0)==0 && count==3);
  std::puts("PASS: recorded Sony Vol+/Power, standard NEC, 3 identical post-deglitch fragmentation variants, malformed NEC fallback, original RAW unchanged");
}
