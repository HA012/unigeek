#include "darkside_recover.h"
#include "crapto1.h"
#include <stdlib.h>
#include <string.h>

static uint32_t fastfwd[2][8] = {
  { 0, 0x4BC53, 0xECB1, 0x450E2, 0x25E29, 0x6E27A, 0x2B298, 0x60ECB},
  { 0, 0x1D962, 0x4BC53, 0x56531, 0xECB1, 0x135D3, 0x450E2, 0x58980}
};

static uint32_t *lfsr_prefix_ks(uint8_t ks[8], int isodd) {
  uint32_t *candidates = (uint32_t *)malloc(4 << 10);
  if (!candidates) return 0;
  int size = 0;
  for (uint32_t i = 0; i < (1u << 21); ++i) {
    int good = 1;
    for (uint32_t c = 0; good && c < 8; ++c) {
      uint32_t entry = i ^ fastfwd[isodd][c];
      good &= (BIT(ks[c], isodd) == filter(entry >> 1));
      good &= (BIT(ks[c], isodd + 2) == filter(entry));
    }
    if (good) candidates[size++] = i;
  }
  candidates[size] = (uint32_t)-1;
  return candidates;
}

static int check_one(uint32_t prefix, uint32_t rresp, uint8_t parities[8][8],
                     uint32_t odd, uint32_t even, uint32_t uid_xor_nt,
                     uint8_t outKey[6]) {
  struct Crypto1State sl;
  int good = 1;
  for (uint32_t c = 0; good && c < 8; ++c) {
    sl.odd = odd ^ fastfwd[1][c];
    sl.even = even ^ fastfwd[0][c];
    lfsr_rollback_bit(&sl, 0, 0);
    lfsr_rollback_bit(&sl, 0, 0);
    uint32_t ks3 = lfsr_rollback_bit(&sl, 0, 0);
    uint32_t ks2 = lfsr_rollback_word(&sl, 0, 0);
    uint32_t ks1 = lfsr_rollback_word(&sl, prefix | c << 5, 1);
    uint32_t nr = ks1 ^ (prefix | c << 5);
    uint32_t rr = ks2 ^ rresp;
    good &= parity(nr & 0x000000ff) ^ parities[c][3] ^ BIT(ks2, 24);
    good &= parity(rr & 0xff000000) ^ parities[c][4] ^ BIT(ks2, 16);
    good &= parity(rr & 0x00ff0000) ^ parities[c][5] ^ BIT(ks2,  8);
    good &= parity(rr & 0x0000ff00) ^ parities[c][6] ^ BIT(ks2,  0);
    good &= parity(rr & 0x000000ff) ^ parities[c][7] ^ ks3;
  }
  if (!good) return 0;
  lfsr_rollback_word(&sl, uid_xor_nt, 0);
  uint64_t key = 0;
  crypto1_get_lfsr(&sl, &key);
  for (int i = 5; i >= 0; --i) {
    outKey[i] = (uint8_t)(key & 0xFF);
    key >>= 8;
  }
  return 1;
}

int darkside_recover_key(uint32_t uid, uint32_t nt, uint64_t par_info,
                         uint64_t ks_info, uint32_t nr, uint32_t ar,
                         uint8_t outKey[6]) {
  uint8_t ks3x[8], par[8][8];
  uint32_t pos, i;
  nr &= 0xffffff1f;
  for (pos = 0; pos < 8; pos++) {
    ks3x[7 - pos] = (uint8_t)((ks_info >> (pos * 8)) & 0x0f);
    uint8_t bt = (uint8_t)((par_info >> (pos * 8)) & 0xff);
    for (i = 0; i < 8; i++) par[7 - pos][i] = (bt >> i) & 1;
  }

  uint32_t *odd = lfsr_prefix_ks(ks3x, 1);
  uint32_t *even = lfsr_prefix_ks(ks3x, 0);
  if (!odd || !even) {
    free(odd);
    free(even);
    return 0;
  }

  int found = 0;
  for (uint32_t *o = odd; *o + 1; ++o) {
    for (uint32_t *e = even; *e + 1; ++e) {
      uint32_t ov = *o, ev = *e;
      for (uint32_t top = 0; top < 64; ++top) {
        ov += 1u << 21;
        ev += (!(top & 7) + 1u) << 21;
        if (check_one(nr, ar, par, ov, ev, uid ^ nt, outKey)) {
          found = 1;
          goto done;
        }
      }
    }
  }
done:
  free(odd);
  free(even);
  return found;
}
