//
// Created by L Shaf on 2026-03-25.
//

#pragma once

#include <Arduino.h>
#include <array>
#include "utils/nfc/MfcKeyStore.h"

class NFCUtility
{
public:
  using mfKey = std::array<uint8_t, 6>;

  struct MIFARE_Key {
    bool has_value = false;
    mfKey key{};

    MIFARE_Key() = default;
    explicit MIFARE_Key(const mfKey& k) : has_value(true), key(k) {}
    MIFARE_Key(byte i0, byte i1, byte i2, byte i3, byte i4, byte i5) {
      key = {i0, i1, i2, i3, i4, i5};
      has_value = true;
    }

    void reset() { has_value = false; key = {}; }
    void set(const mfKey& k) { key = k; has_value = true; }

    bool has() const { return has_value; }
    explicit operator bool() const { return has_value; }

    const mfKey& value() const { return key; }
    mfKey& value() { return key; }

    std::string c_str() const {
      if (!has_value) return "??:??:??:??:??:??";
      char buffer[20];
      sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
        key[0], key[1], key[2], key[3], key[4], key[5]);
      return buffer;
    }
  };

  static std::array<MIFARE_Key, MfcKeyStore::kDefaultKeyCount> getDefaultKeys() {
    std::array<MIFARE_Key, MfcKeyStore::kDefaultKeyCount> out{};
    for (size_t i = 0; i < MfcKeyStore::kDefaultKeyCount; ++i) {
      mfKey key{};
      memcpy(key.data(), MfcKeyStore::kDefaultKeys[i], key.size());
      out[i] = MIFARE_Key(key);
    }
    return out;
  }
};
