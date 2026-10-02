#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
struct nvs_stats_t { uint32_t used_entries=0, free_entries=400; };
inline esp_err_t nvs_get_stats(const char *, nvs_stats_t *) { return ESP_OK; }
