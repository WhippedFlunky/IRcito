#include "text_editor.h"
#include <cstring>

namespace editor {
namespace {
constexpr char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_+";
}
void State::start(const char *original) {
  std::memset(text,0,sizeof(text));
  if (original) std::strncpy(text,original,sizeof(text)-1);
  cursor=uint8_t(std::strlen(text));candidate=0;
}
void State::next() {candidate=uint8_t((candidate+1)%(sizeof(kChars)-1));}
char State::current() const {return kChars[candidate];}
void State::accept() {
  if (cursor>=sizeof(text)-1) return;
  text[cursor++]=current();text[cursor]=0;candidate=0;
}
} // namespace editor
