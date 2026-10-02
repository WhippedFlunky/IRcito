#pragma once
#include <cstdarg>
#include <cstdio>
struct FakeSerial {
  template <typename... T> void printf(const char *, T...) {}
  void println(const char *) {}
};
inline FakeSerial Serial;
