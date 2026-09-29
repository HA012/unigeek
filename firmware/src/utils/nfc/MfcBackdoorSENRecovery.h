#pragma once

#include <cstdint>

namespace MfcBackdoorSENRecovery {

struct Result {
  bool success = false;
  uint8_t recovered = 0;
  bool foundA[40] = {};
  bool foundB[40] = {};
  uint8_t keysA[40][6] = {};
  uint8_t keysB[40][6] = {};
};

using ProgressFn = void(*)(const char* msg, int pct);

// Runs the Chameleon Ultra Backdoor Assisted Static Encrypted Nested flow.
// Existing keys are accepted so already-recovered sector keys are not retried.
Result run(uint8_t sectors,
           const bool foundA[40], const bool foundB[40],
           const uint8_t keysA[40][6], const uint8_t keysB[40][6],
           ProgressFn progress = nullptr);

} // namespace MfcBackdoorSENRecovery
