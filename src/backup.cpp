#include "backup.h"
#include "macros.h"
#include "storage.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace backup {
namespace {
constexpr char hex[] = "0123456789ABCDEF";
store::Signal signalBuffer; // Internal BSS; never a 1 KiB stack allocation.
char output[2400];
uint32_t crc32(const char *p, size_t n) {
  uint32_t c = ~uint32_t(0);
  for (size_t i=0; i<n; ++i) {
    c ^= uint8_t(p[i]);
    for (unsigned j=0;j<8;++j) c=(c>>1)^(0xedb88320u & (0u-(c&1)));
  }
  return ~c;
}
bool format(char *out, size_t cap, size_t &n, const char *fmt,
            unsigned a, unsigned b=0, unsigned c=0, unsigned d=0) {
  if (n>=cap) return false;
  int written=std::snprintf(out+n,cap-n,fmt,a,b,c,d);
  if (written<0 || size_t(written)>=cap-n) return false;
  n+=size_t(written); return true;
}
bool nameHex(char *out, size_t cap, size_t &n, const char *name) {
  if (!store::validName(name)) return false;
  for (size_t i=0;name[i];++i) {
    if (n+2>=cap) return false;
    const uint8_t byte=uint8_t(name[i]);
    out[n++]=hex[byte>>4]; out[n++]=hex[byte&15];
  }
  out[n]=0; return true;
}
bool finish(Write write, void *ctx, size_t n) {
  if (!format(output,sizeof(output),n,"|%08X",crc32(output,n))) return false;
  write(output,ctx); return true;
}
int digit(char c) {
  if (c>='0'&&c<='9') return c-'0';
  if (c>='A'&&c<='F') return c-'A'+10;
  return -1;
}
bool parseName(const char *hexName, char (&name)[20]) {
  const size_t n=std::strlen(hexName);
  if (!n || n>38 || (n&1)) return false;
  std::memset(name,0,sizeof(name));
  for (size_t i=0;i<n/2;++i) {
    int a=digit(hexName[i*2]), b=digit(hexName[i*2+1]);
    if (a<0||b<0) return false;
    name[i]=char((a<<4)|b);
  }
  return store::validName(name);
}
bool number(const char *s, uint32_t &out) {
  if (!s||!*s) return false;
  uint32_t value=0;
  for (const char *p=s;*p;++p) {
    if (*p<'0'||*p>'9'||value>(UINT32_MAX-uint32_t(*p-'0'))/10) return false;
    value=value*10+uint32_t(*p-'0');
  }
  out=value; return true;
}
bool number(const char *s, uint32_t &out, uint32_t max) {
  return number(s,out)&&out<=max;
}
} // namespace

bool exportAll(Write write, void *ctx) {
  if (!write || !store::available()) return false;
  write("IRCITO-BACKUP|1",ctx);
  for (uint8_t r=0;r<store::kRemotes;++r) {
    store::Remote remote;
    store::loadRemote(r,remote);
    size_t n=0; output[0]=0;
    if (!format(output,sizeof(output),n,"R|%u|",r) ||
        !nameHex(output,sizeof(output),n,remote.name) || !finish(write,ctx,n)) return false;
    for (uint8_t b=0;b<store::kButtons;++b) {
      if (!store::loadSignalForExport(r,b,signalBuffer)) continue;
      n=0;output[0]=0;
      const auto &s=signalBuffer;
      if (!format(output,sizeof(output),n,"S|%u|%u|",r,b) ||
          !nameHex(output,sizeof(output),n,s.name) ||
          !format(output,sizeof(output),n,"|%u|%u|%u|%u|",
                  s.decoded.valid ? unsigned(s.decoded.protocol) : 0u,
                  s.carrierHz,s.decoded.address,s.decoded.command) ||
          !format(output,sizeof(output),n,"%u|%u|%u|%u|",
                  s.decoded.valid ? s.decoded.bits : 0u,s.repeatCount,
                  s.repeatPeriodUs,s.rawCount)) return false;
      for (size_t i=0;i<s.rawCount;++i)
        if (!format(output,sizeof(output),n,"%08X",s.raw[i].val)) return false;
      if (!finish(write,ctx,n)) return false;
    }
  }
  for (uint8_t slot=0;slot<macros::kSlots;++slot) {
    macros::Macro m;
    if (!macros::load(slot,m)) continue;
    size_t n=0;output[0]=0;
    if (!format(output,sizeof(output),n,"M|%u|",slot) ||
        !nameHex(output,sizeof(output),n,m.name) ||
        !format(output,sizeof(output),n,"|%u|",m.stepCount)) return false;
    for (uint8_t i=0;i<m.stepCount;++i) {
      if (!format(output,sizeof(output),n,"%u,%u,%u;",m.steps[i].remote,
                  m.steps[i].button,m.steps[i].delayAfterMs)) return false;
    }
    if (!finish(write,ctx,n)) return false;
  }
  write("END",ctx);
  return true;
}

void Importer::reset() {
  length_=0; line_[0]=0; started_=finished_=false; imported_=errors_=0;
}
bool Importer::feed(char c) {
  if (c=='\r') return true;
  if (c=='\n') {
    if (!length_) return true;
    line_[length_]=0;
    const bool ok=process(); length_=0;line_[0]=0;
    if (!ok) ++errors_;
    return ok;
  }
  if (length_>=sizeof(line_)-1) {length_=0;line_[0]=0;++errors_;return false;}
  line_[length_++]=c; return true;
}
bool Importer::process() {
  if (!started_) {
    if (std::strcmp(line_,"IRCITO-BACKUP|1")) return false;
    started_=true; return true;
  }
  if (finished_) return false;
  if (!std::strcmp(line_,"END")) {finished_=true;return true;}
  char *last=std::strrchr(line_,'|');
  if (!last || std::strlen(last+1)!=8) return false;
  uint32_t expected=0;
  for (const char *p=last+1;*p;++p) {
    int d=digit(*p);if(d<0)return false;expected=(expected<<4)|uint32_t(d);
  }
  if (crc32(line_,size_t(last-line_))!=expected) return false;
  *last=0;
  // strtok is bounded by the complete per-record buffer, and empty fields
  // are rejected by the expected field count checks below.
  char *fields[13]={}; size_t count=0;
  char *cursor=line_;
  while (count<13) {
    fields[count++]=cursor;
    char *sep=std::strchr(cursor,'|');
    if (!sep) break;
    *sep=0; cursor=sep+1;
  }
  if (count==3 && !std::strcmp(fields[0],"R")) {
    uint32_t r;
    store::Remote remote={};
    if (!number(fields[1],r,store::kRemotes-1) || !parseName(fields[2],remote.name)) return false;
    remote.id=r+1;
    if (!store::saveRemote(uint8_t(r),remote)) return false;
  } else if (count==13 && !std::strcmp(fields[0],"S")) {
    uint32_t r,b,p,carrier,address,command,bits,repeats,period,rawCount;
    if (!number(fields[1],r,store::kRemotes-1) ||
        !number(fields[2],b,store::kButtons-1) ||
        !parseName(fields[3],signalBuffer.name) ||
        !number(fields[4],p,2) || !number(fields[5],carrier) ||
        !number(fields[6],address) || !number(fields[7],command) ||
        !number(fields[8],bits,32) || !number(fields[9],repeats,10) ||
        !number(fields[10],period,1000000) ||
        !number(fields[11],rawCount,store::kMaxRaw) ||
        std::strlen(fields[12]) != rawCount*8) return false;
    signalBuffer.id=r*store::kButtons+b+1;
    signalBuffer.carrierHz=carrier;
    signalBuffer.decoded = p ? ir::DecodeResult{static_cast<ir::Protocol>(p),address,command,uint16_t(bits),true} : ir::DecodeResult{};
    if (!p && bits) return false;
    signalBuffer.repeatCount=repeats;
    signalBuffer.repeatPeriodUs=period;
    signalBuffer.rawCount=uint16_t(rawCount);
    for (size_t i=0;i<rawCount;++i) {
      uint32_t value=0;
      for (unsigned j=0;j<8;++j) {
        int d=digit(fields[12][i*8+j]);if(d<0)return false;
        value=(value<<4)|uint32_t(d);
      }
      signalBuffer.raw[i].val=value;
    }
    if (!store::saveSignal(uint8_t(r),uint8_t(b),signalBuffer)) return false;
  } else if (count==5 && !std::strcmp(fields[0],"M")) {
    uint32_t slot,steps;
    macros::Macro m;
    if (!number(fields[1],slot,macros::kSlots-1) ||
        !number(fields[3],steps,macros::kMaxSteps) ||
        !parseName(fields[2],m.name)) return false;
    m.id=slot+1;m.stepCount=uint8_t(steps);
    const char *s=fields[4];
    for (size_t i=0;i<steps;++i) {
      uint32_t v[3]={};
      for (unsigned j=0;j<3;++j) {
        const char *begin=s;
        while (*s && *s!=',' && *s!=';') ++s;
        char tmp[12];const size_t len=size_t(s-begin);
        if (!len || len>=sizeof(tmp)) return false;
        std::memcpy(tmp,begin,len);tmp[len]=0;
        if (!number(tmp,v[j])) return false;
        if (*s != (j==2?';':',')) return false;
        ++s;
      }
      if (v[0]>=store::kRemotes || v[1]>=store::kButtons || v[2]>UINT16_MAX) return false;
      m.steps[i]={uint8_t(v[0]),uint8_t(v[1]),uint16_t(v[2])};
    }
    if (*s || !macros::valid(m,uint8_t(slot)) || !macros::save(uint8_t(slot),m)) return false;
  } else return false;
  ++imported_; return true;
}
} // namespace backup
