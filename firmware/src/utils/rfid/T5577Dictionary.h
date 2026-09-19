#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

namespace T5577Dictionary {

static constexpr const char* kDirectory = "/unigeek/rfid/dictionaries";

static constexpr uint8_t kBuiltinKeys[][4] = {
    {0x51, 0x24, 0x36, 0x48}, {0x00, 0x00, 0x00, 0x00},
    {0xAA, 0xAA, 0xAA, 0xAA}, {0x55, 0x55, 0x55, 0x55},
    {0x12, 0x34, 0x56, 0x78}, {0xFF, 0xFF, 0xFF, 0xFF},
    {0x19, 0x92, 0x04, 0x27}, {0x01, 0x23, 0x45, 0x67},
    {0xAB, 0xCD, 0xEF, 0x01}, {0xC6, 0xB6, 0xF9, 0x2E},
};

static constexpr size_t kBuiltinKeyCount = sizeof(kBuiltinKeys) / sizeof(kBuiltinKeys[0]);

inline bool parseKey(String value, uint8_t out[4]) {
  if (!out) return false;
  value.trim();
  if (!value.length() || value.startsWith("#")) return false;
  value.replace(":", "");
  value.replace(" ", "");
  if (value.length() != 8) return false;

  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };

  for (uint8_t i = 0; i < 4; ++i) {
    const int hi = nibble(value[i * 2]);
    const int lo = nibble(value[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

}  // namespace T5577Dictionary
