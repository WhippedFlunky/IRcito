#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct Preferences {
  inline static std::map<std::string,std::vector<uint8_t>> values;
  inline static bool failBlob = false, failIndex = false;
  std::string ns;
  bool begin(const char *name, bool) { ns = name; return true; }
  std::string key(const char *k) const { return ns + "/" + k; }
  bool isKey(const char *k) const { return values.count(key(k)); }
  size_t getBytesLength(const char *k) const {
    auto it = values.find(key(k)); return it == values.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char *k, void *v, size_t n) const {
    auto it = values.find(key(k));
    if (it == values.end() || it->second.size() != n) return 0;
    std::memcpy(v, it->second.data(), n); return n;
  }
  size_t putBytes(const char *k, const void *v, size_t n) {
    if (failBlob) return 0;
    auto *p = static_cast<const uint8_t *>(v);
    values[key(k)] = std::vector<uint8_t>(p, p+n); return n;
  }
  uint8_t getUChar(const char *k, uint8_t d=0) const {
    auto it = values.find(key(k)); return it == values.end() || it->second.size() != 1 ? d : it->second[0];
  }
  size_t putUChar(const char *k, uint8_t v) {
    if (failIndex) return 0;
    values[key(k)] = {v}; return 1;
  }
  bool remove(const char *k) { return values.erase(key(k)) > 0; }
  bool clear() {
    for (auto it = values.begin(); it != values.end();) {
      if (it->first.compare(0, ns.size()+1, ns + "/") == 0) it = values.erase(it);
      else ++it;
    }
    return true;
  }
};
