#pragma once
#include <cstdint>
enum {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS};
struct esp_partition_t { uint32_t size; };
inline const esp_partition_t *esp_partition_find_first(int,int,const char *) {
  static const esp_partition_t nvs = {20480}; return &nvs;
}
