#pragma once
#include <stdint.h>
typedef union {
  struct {
    uint32_t duration0 : 15;
    uint32_t level0 : 1;
    uint32_t duration1 : 15;
    uint32_t level1 : 1;
  };
  uint32_t val;
} rmt_symbol_word_t;
