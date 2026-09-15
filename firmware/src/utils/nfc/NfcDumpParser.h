#pragma once
#include <stddef.h>
#include <stdint.h>

class NfcDumpParser {
public:
  enum Type {
    TYPE_UNKNOWN,
    TYPE_MIFARE_CLASSIC_MINI,
    TYPE_MIFARE_CLASSIC_1K,
    TYPE_MIFARE_CLASSIC_2K,
    TYPE_MIFARE_CLASSIC_4K,
    TYPE_NTAG210,
    TYPE_NTAG212,
    TYPE_NTAG213,
    TYPE_NTAG215,
    TYPE_NTAG216,
  };

  struct Info {
    Type type = TYPE_UNKNOWN;
    size_t size = 0;
    uint8_t uid[7] = {};
    uint8_t uidLen = 0;
    bool uidValid = false;
  };

  // Identify the raw dump format from the exact image size used by UniGeek.
  static Type typeForSize(size_t size);
  static const char* typeName(Type type);
  static bool isMifareClassic(Type type);
  static bool isType2(Type type);

  // Inspect format and manufacturer bytes. A recognized dump may still have
  // uidValid == false when its UID/BCC layout is malformed or unsupported.
  static Info inspect(const uint8_t* dump, size_t dumpLen);
};
