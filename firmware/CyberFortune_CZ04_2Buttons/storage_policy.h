#pragma once
#include <stdint.h>

inline uint32_t storageUploadLimit(bool large, bool vocabulary, uint32_t freeBytes) {
  const uint32_t reserve = large ? 256UL * 1024UL : 64UL * 1024UL;
  const uint32_t cap = large ? (vocabulary ? 2UL : 8UL) * 1024UL * 1024UL
                             : 256UL * 1024UL;
  if (freeBytes <= reserve) return 0;
  const uint32_t available = freeBytes - reserve;
  return available < cap ? available : cap;
}
