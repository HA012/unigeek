#pragma once

#include <stdint.h>

class TagPrompt {
public:
  // title may be null to leave the existing header untouched.
  static void show(const char* message, int16_t x, int16_t y, int16_t w, int16_t h,
                   const char* title = nullptr);
};
