#pragma once

#include <stdint.h>
#include <string.h>
#include <stdio.h>

struct MfcRecoverySummary {
  uint8_t values[80][6] = {};
  uint8_t count = 0;

  void reset() { count = 0; }

  void add(const uint8_t key[6]) {
    for (uint8_t i = 0; i < count; ++i)
      if (memcmp(values[i], key, 6) == 0) return;
    if (count < 80) memcpy(values[count++], key, 6);
  }

  void format(char* out, size_t capacity, unsigned found, unsigned totalKeys,
              unsigned covered, unsigned totalSectors) const {
    snprintf(out, capacity, "%u/%u keys recovered\n%u/%u sectors authenticated",
             found, totalKeys, covered, totalSectors);
  }
};
