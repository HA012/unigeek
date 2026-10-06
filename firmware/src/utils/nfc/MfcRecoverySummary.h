#pragma once

#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Count distinct 6-byte key values newly authenticated in this operation,
// independently of the number of sector A/B slots they populate.
struct MfcRecoverySummary {
  uint8_t values[80][6] = {};
  uint8_t count = 0;

  void reset() { count = 0; }

  void add(const uint8_t key[6]) {
    for (uint8_t i = 0; i < count; ++i)
      if (memcmp(values[i], key, 6) == 0) return;
    if (count < 80) memcpy(values[count++], key, 6);
  }

  void format(char* out, size_t capacity, unsigned covered, unsigned total) const {
    snprintf(out, capacity, "%u %s recovered\n%u/%u sectors authenticated",
             (unsigned)count, count == 1 ? "key" : "keys", covered, total);
  }
};
