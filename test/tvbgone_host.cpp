#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>
#include "../src/tvbgone.h"

int main() {
  assert(tvbgone::count(tvbgone::Region::NorthAmerica) == 137);
  assert(tvbgone::count(tvbgone::Region::Europe) == 137);
  rmt_symbol_word_t output[512];
  uint64_t maxUs = 0;
  size_t nonPulsed = 0;
  for (auto region : {tvbgone::Region::NorthAmerica, tvbgone::Region::Europe}) {
    for (size_t i = 0; i < tvbgone::count(region); ++i) {
      const auto *code = tvbgone::code(region, i);
      assert(code);
      if (!code->hz) ++nonPulsed;
      else assert(code->hz >= 25000 && code->hz <= 85000);
      const size_t count = tvbgone::encode(*code, output, 512);
      assert(count > 0 && count <= 512);
      uint64_t actual = 0, original = 0;
      std::vector<std::pair<bool,uint64_t>> expectedRuns, outputRuns;
      auto append = [](auto &runs, bool level, uint32_t duration) {
        if (!duration) return;
        if (!runs.empty() && runs.back().first == level) runs.back().second += duration;
        else runs.emplace_back(level, duration);
      };
      for (size_t j = 0; j < count; ++j)
      {
        actual += output[j].duration0 + output[j].duration1;
        append(outputRuns, output[j].level0, output[j].duration0);
        append(outputRuns, output[j].level1, output[j].duration1);
      }
      for (size_t p = 0; p < code->pairs; ++p) {
        unsigned index = 0;
        for (unsigned bit = 0; bit < code->bits; ++bit) {
          const size_t pos = p * code->bits + bit;
          index = (index << 1) | ((code->indices[pos / 8] >> (7 - pos % 8)) & 1);
        }
        assert(index < code->timingPairs);
        original += 10u * uint32_t(code->times[index * 2] + code->times[index * 2 + 1]);
        append(expectedRuns, true, 10u * code->times[index * 2]);
        append(expectedRuns, false, 10u * code->times[index * 2 + 1]);
      }
      assert(actual == original);
      assert(outputRuns == expectedRuns);
      if (actual > maxUs) maxUs = actual;
    }
  }
  assert(nonPulsed > 0);
  std::printf("PASS: 137 NA + 137 EU codes encode losslessly; %zu non-carrier entries, longest %.3f s\n",
              nonPulsed, maxUs / 1e6);
}
