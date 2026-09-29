#pragma once

#include <Arduino.h>
#include <cstring>
#include "core/IStorage.h"

namespace MfcKeyStore {

static constexpr const char* kDictionaryDir = "/unigeek/nfc/dictionaries";
static constexpr const char* kDiscoveredDictionary = "/unigeek/nfc/dictionaries/discovered.txt";

inline bool containsKeyLine(const String& content, const String& key) {
  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    if (line.equalsIgnoreCase(key)) return true;
    start = nl + 1;
  }
  return false;
}

// Add every key present in the per-UID persisted-key buffer to the global
// discovered dictionary. The input format is: "Sxx A|B 12HEXCHARS".
// Existing entries are preserved and duplicates are ignored case-insensitively.
inline void updateDiscoveredDictionary(IStorage* storage, const String& persistedKeys) {
  if (!storage || !storage->isAvailable() || persistedKeys.length() == 0) return;

  storage->makeDir(kDictionaryDir);
  String discovered = storage->readFile(kDiscoveredDictionary);
  bool changed = false;

  int start = 0;
  while (start < (int)persistedKeys.length()) {
    int nl = persistedKeys.indexOf('\n', start);
    if (nl < 0) nl = persistedKeys.length();
    String line = persistedKeys.substring(start, nl);
    line.trim();

    int sector = -1;
    char keyType = 0;
    char hex[13] = {};
    if (sscanf(line.c_str(), "S%d %c %12s", &sector, &keyType, hex) == 3) {
      String key(hex);
      key.toUpperCase();
      bool validHex = key.length() == 12;
      for (int i = 0; validHex && i < 12; ++i) {
        const char c = key[i];
        validHex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
      }
      if (validHex && !containsKeyLine(discovered, key)) {
        if (discovered.length() && !discovered.endsWith("\n")) discovered += '\n';
        discovered += key;
        discovered += '\n';
        changed = true;
      }
    }
    start = nl + 1;
  }

  if (changed) storage->writeFile(kDiscoveredDictionary, discovered.c_str());
}

inline void saveUidKeys(IStorage* storage,
                        const uint8_t* uid, uint8_t uidLen, uint8_t sectors,
                        const bool foundA[40], const bool foundB[40],
                        const uint8_t keysA[40][6], const uint8_t keysB[40][6]) {
  if (!storage || !storage->isAvailable() || !uid || !uidLen || !sectors) return;
  if (sectors > 40) sectors = 40;

  storage->makeDir("/unigeek/nfc/keys");
  char uidHex[16] = {};
  for (uint8_t i = 0; i < uidLen && i * 2 + 2 < (int)sizeof(uidHex); i++) {
    char h[4];
    snprintf(h, sizeof(h), "%02X", uid[i]);
    strcat(uidHex, h);
  }
  String path = String("/unigeek/nfc/keys/") + uidHex + ".txt";
  String buf;
  for (uint8_t s = 0; s < sectors; s++) {
    char line[48];
    if (foundA[s]) {
      snprintf(line, sizeof(line), "S%02d A %02X%02X%02X%02X%02X%02X\n",
               s, keysA[s][0], keysA[s][1], keysA[s][2],
               keysA[s][3], keysA[s][4], keysA[s][5]);
      buf += line;
    }
    if (foundB[s]) {
      snprintf(line, sizeof(line), "S%02d B %02X%02X%02X%02X%02X%02X\n",
               s, keysB[s][0], keysB[s][1], keysB[s][2],
               keysB[s][3], keysB[s][4], keysB[s][5]);
      buf += line;
    }
  }
  if (buf.length() == 0) return;
  storage->writeFile(path.c_str(), buf.c_str());
  updateDiscoveredDictionary(storage, buf);
}

} // namespace MfcKeyStore
