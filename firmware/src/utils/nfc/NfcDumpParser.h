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
    bool hasNdef = false;
    size_t ndefLen = 0;
  };

  // Identify the raw dump format from the exact image size used by UniGeek.
  static Type typeForSize(size_t size);
  static const char* typeName(Type type);
  static bool isMifareClassic(Type type);
  static bool isType2(Type type);
  static size_t mifareClassicSectorCount(Type type);

  // Extract the NDEF message from a recognized dump. On success `*ndef` is
  // heap-allocated and must be released by the caller with delete[]. This
  // deliberately hides the different ownership rules of the underlying
  // Classic and Type-2 extractors.
  static bool extractNdef(const uint8_t* dump, size_t dumpLen,
                          uint8_t** ndef, size_t* ndefLen);

  // Replace the NDEF Message TLV in an already NDEF-formatted dump while
  // preserving UID, MAD, trailers, lock/configuration bytes and other
  // metadata. Supports Classic dumps whose MAD marks NFC Forum sectors and
  // the NTAG21x layouts recognized by this parser. On failure the dump is
  // left unchanged.
  static bool replaceNdef(uint8_t* dump, size_t dumpLen,
                          const uint8_t* ndef, size_t ndefLen);

  // Replace the UID in a recognized raw dump and update its BCC bytes.
  // Currently supports 4-byte MIFARE Classic and 7-byte NTAG21x layouts.
  // Validation is completed before the dump is modified; on failure the
  // input buffer is left unchanged.
  static bool setUid(uint8_t* dump, size_t dumpLen,
                     const uint8_t* uid, size_t uidLen);

  // Inspect format and manufacturer bytes. A recognized dump may still have
  // uidValid == false when its UID/BCC layout is malformed or unsupported.
  static Info inspect(const uint8_t* dump, size_t dumpLen);
};
