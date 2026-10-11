#pragma once
#include <stddef.h>
#include <stdint.h>

class HfDumpParser {
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
    bool hasNdef = false;
    size_t ndefLen = 0;
  };

  struct PasswordInfo {
    bool supported = false;
    bool enabled = false;
    uint8_t auth0 = 0xFF;
    uint8_t access = 0;
    bool protectRead = false;
    bool configLocked = false;
    uint8_t authLimit = 0;
    uint8_t pwd[4] = {};
    uint8_t pack[2] = {};
  };

  static Type typeForSize(size_t size);
  static const char* typeName(Type type);
  static bool isMifareClassic(Type type);
  static bool isType2(Type type);
  static size_t mifareClassicSectorCount(Type type);

  static PasswordInfo inspectPassword(const uint8_t* dump, size_t dumpLen);

  static bool setPassword(uint8_t* dump, size_t dumpLen,
                          const uint8_t pwd[4], const uint8_t pack[2],
                          bool protectRead);

  static bool removePassword(uint8_t* dump, size_t dumpLen);

  static bool extractNdef(const uint8_t* dump, size_t dumpLen,
                          uint8_t** ndef, size_t* ndefLen);

  static bool replaceNdef(uint8_t* dump, size_t dumpLen,
                          const uint8_t* ndef, size_t ndefLen);

  static bool setUid(uint8_t* dump, size_t dumpLen,
                     const uint8_t* uid, size_t uidLen);

  static Info inspect(const uint8_t* dump, size_t dumpLen);
};
