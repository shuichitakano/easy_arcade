#pragma once
#include <stdint.h>
uint64_t get_absolute_time();
inline uint32_t to_ms_since_boot(uint64_t time) { return uint32_t(time); }
