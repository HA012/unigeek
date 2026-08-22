#include "NfcDumpBuilder.h"

#include <cstring>


bool NfcDumpBuilder::buildMifareClassic1K(const uint8_t uid[4],
                                         const uint8_t* ndef, size_t ndefLen,
                                         uint8_t* out, size_t& outLen,
                                         size_t maxLen) {
  if (!uid || !out || maxLen < MIFARE_CLASSIC_1K_SIZE) return false;
  if (ndefLen > 0 && !ndef) return false;

  // Sectors 1..15 provide 15 * 3 * 16 = 720 bytes of NDEF data area.
  // TLV overhead is: type + length + terminator (short) or
  // type + FF + 2-byte length + terminator (extended).
  const size_t tlvOverhead = (ndefLen < 0xFF) ? 3 : 5;
  if (ndefLen + tlvOverhead > MIFARE_CLASSIC_1K_NDEF_CAPACITY) return false;

  memset(out, 0x00, MIFARE_CLASSIC_1K_SIZE);

  // Block 0 — manufacturer block. The first five bytes are the 4-byte UID
  // followed by BCC. The remaining bytes mirror a conventional MFC1K image;
  // the Chameleon Ultra treats this file as a raw 64-block dump.
  out[0] = uid[0];
  out[1] = uid[1];
  out[2] = uid[2];
  out[3] = uid[3];
  out[4] = (uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]);
  out[5] = 0x08;
  out[6] = 0x04;
  out[7] = 0x00;
  out[8] = 0x62;
  out[9] = 0x63;
  out[10] = 0x64;
  out[11] = 0x65;
  out[12] = 0x66;
  out[13] = 0x67;
  out[14] = 0x68;
  out[15] = 0x69;

  // MAD1, sector 0 blocks 1 and 2.
  // 0x14 is the MAD CRC for this fixed map; 0x01 is the MAD info byte.
  // Every sector 1..15 is assigned NFC Forum NDEF AID 0x03E1.
  out[16] = 0x14;
  out[17] = 0x01;
  for (size_t i = 0; i < 15; ++i) {
    const size_t p = 18 + i * 2;
    out[p] = 0x03;
    out[p + 1] = 0xE1;
  }

  // MAD sector trailer (block 3).
  static const uint8_t madTrailer[16] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
    0x78, 0x77, 0x88, 0xC1,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  memcpy(&out[3 * 16], madTrailer, sizeof(madTrailer));

  // NFC Forum sector trailers for sectors 1..15.
  static const uint8_t ndefTrailer[16] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7,
    0x7F, 0x07, 0x88, 0x40,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  for (uint8_t sector = 1; sector < 16; ++sector) {
    const size_t trailerBlock = sector * 4 + 3;
    memcpy(&out[trailerBlock * 16], ndefTrailer, sizeof(ndefTrailer));
  }

  // Build the NDEF Message TLV into a contiguous temporary buffer, then
  // distribute it over the three data blocks of each NFC Forum sector,
  // skipping every sector trailer.
  uint8_t tlv[MIFARE_CLASSIC_1K_NDEF_CAPACITY] = {};
  size_t t = 0;
  tlv[t++] = 0x03;
  if (ndefLen < 0xFF) {
    tlv[t++] = (uint8_t)ndefLen;
  } else {
    tlv[t++] = 0xFF;
    tlv[t++] = (uint8_t)((ndefLen >> 8) & 0xFF);
    tlv[t++] = (uint8_t)(ndefLen & 0xFF);
  }
  if (ndefLen > 0) {
    memcpy(&tlv[t], ndef, ndefLen);
    t += ndefLen;
  }
  tlv[t++] = 0xFE;

  size_t src = 0;
  for (uint8_t sector = 1; sector < 16 && src < t; ++sector) {
    for (uint8_t blockInSector = 0; blockInSector < 3 && src < t; ++blockInSector) {
      const size_t block = sector * 4 + blockInSector;
      const size_t n = (t - src > 16) ? 16 : (t - src);
      memcpy(&out[block * 16], &tlv[src], n);
      src += n;
    }
  }

  outLen = MIFARE_CLASSIC_1K_SIZE;
  return true;
}


bool NfcDumpBuilder::buildNtag215(const uint8_t uid[7],
                                 const uint8_t* ndef, size_t ndefLen,
                                 uint8_t* out, size_t& outLen, size_t maxLen) {
  if (!uid || !out || maxLen < NTAG215_SIZE) return false;
  if (ndefLen > 0 && !ndef) return false;

  // NTAG215 dump: 135 pages x 4 bytes = 540 bytes (pages 0..134).
  // The CC advertises the standard 496-byte NFC Forum Type 2 data area.
  const size_t tlvOverhead = (ndefLen < 0xFF) ? 3 : 5;
  if (ndefLen + tlvOverhead > NTAG215_NDEF_CAPACITY) return false;

  memset(out, 0x00, NTAG215_SIZE);

  // Tag page 0.
  size_t off = 0;
  out[off + 0] = uid[0];
  out[off + 1] = uid[1];
  out[off + 2] = uid[2];
  out[off + 3] = (uint8_t)(0x88 ^ uid[0] ^ uid[1] ^ uid[2]); // BCC0

  // Tag page 1.
  off = 4;
  out[off + 0] = uid[3];
  out[off + 1] = uid[4];
  out[off + 2] = uid[5];
  out[off + 3] = uid[6];

  // Tag page 2.
  off = 8;
  out[off + 0] = (uint8_t)(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]); // BCC1
  out[off + 1] = 0x48;
  out[off + 2] = 0x00;
  out[off + 3] = 0x00;

  // Tag page 3 — Capability Container.
  off = 12;
  out[off + 0] = 0xE1;
  out[off + 1] = 0x10;
  out[off + 2] = 0x3E;
  out[off + 3] = 0x00;

  // Tag page 4 onward — NDEF Message TLV.
  size_t p = 16;
  out[p++] = 0x03;
  if (ndefLen < 0xFF) {
    out[p++] = (uint8_t)ndefLen;
  } else {
    out[p++] = 0xFF;
    out[p++] = (uint8_t)((ndefLen >> 8) & 0xFF);
    out[p++] = (uint8_t)(ndefLen & 0xFF);
  }

  if (ndefLen > 0) {
    memcpy(&out[p], ndef, ndefLen);
    p += ndefLen;
  }
  out[p++] = 0xFE;

  // NTAG215 configuration tail:
  // page 130 dynamic locks
  off = 130 * 4;
  out[off + 0] = 0x00;
  out[off + 1] = 0x00;
  out[off + 2] = 0x00;
  out[off + 3] = 0xBD;

  // page 131 CFG0
  off = 131 * 4;
  out[off + 0] = 0x04;
  out[off + 1] = 0x00;
  out[off + 2] = 0x00;
  out[off + 3] = 0xFF;

  // page 132 CFG1 / ACCESS.
  off = 132 * 4;
  out[off + 0] = 0x00;
  out[off + 1] = 0x05;
  out[off + 2] = 0x00;
  out[off + 3] = 0x00;

  // page 133 PWD.
  off = 133 * 4;
  out[off + 0] = 0x00;
  out[off + 1] = 0x00;
  out[off + 2] = 0x00;
  out[off + 3] = 0x00;

  outLen = NTAG215_SIZE;
  return true;
}
