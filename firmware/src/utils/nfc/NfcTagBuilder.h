#pragma once

#include <stddef.h>
#include <stdint.h>

class NfcTagBuilder {
public:
  static constexpr size_t MIFARE_CLASSIC_1K_SIZE = 1024;
  static constexpr size_t MIFARE_CLASSIC_1K_NDEF_CAPACITY = 720;

  static constexpr size_t NTAG215_SIZE = 540;
  static constexpr size_t NTAG215_NDEF_CAPACITY = 496;

  // Build a raw 64-block MIFARE Classic 1K image formatted as an
  // NFC Forum NDEF tag (MAD1 + NFC Forum sectors).
  // `ndef == nullptr && ndefLen == 0` builds an empty NDEF-formatted tag.
  static bool buildMifareClassic1K(const uint8_t uid[4],
                                   const uint8_t* ndef, size_t ndefLen,
                                   uint8_t* out, size_t& outLen, size_t maxLen);

  // Build a 540-byte Chameleon Ultra NTAG215 .bin image.
  // `ndef == nullptr && ndefLen == 0` builds an empty NDEF-formatted tag.
  static bool buildNtag215(const uint8_t uid[7],
                           const uint8_t* ndef, size_t ndefLen,
                           uint8_t* out, size_t& outLen, size_t maxLen);
};
