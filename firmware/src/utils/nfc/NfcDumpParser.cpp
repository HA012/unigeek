#include "NfcDumpParser.h"
#include <string.h>

NfcDumpParser::Type NfcDumpParser::typeForSize(size_t size) {
  switch (size) {
    case 320:  return TYPE_MIFARE_CLASSIC_MINI;
    case 1024: return TYPE_MIFARE_CLASSIC_1K;
    case 2048: return TYPE_MIFARE_CLASSIC_2K;
    case 4096: return TYPE_MIFARE_CLASSIC_4K;
    case 80:   return TYPE_NTAG210;
    case 164:  return TYPE_NTAG212;
    case 180:  return TYPE_NTAG213;
    case 540:  return TYPE_NTAG215;
    case 924:  return TYPE_NTAG216;
    default:   return TYPE_UNKNOWN;
  }
}

const char* NfcDumpParser::typeName(Type type) {
  switch (type) {
    case TYPE_MIFARE_CLASSIC_MINI: return "MIFARE Classic Mini";
    case TYPE_MIFARE_CLASSIC_1K:   return "MIFARE Classic 1K";
    case TYPE_MIFARE_CLASSIC_2K:   return "MIFARE Classic 2K";
    case TYPE_MIFARE_CLASSIC_4K:   return "MIFARE Classic 4K";
    case TYPE_NTAG210: return "NTAG210";
    case TYPE_NTAG212: return "NTAG212";
    case TYPE_NTAG213: return "NTAG213";
    case TYPE_NTAG215: return "NTAG215";
    case TYPE_NTAG216: return "NTAG216";
    default: return "Unknown";
  }
}

bool NfcDumpParser::isMifareClassic(Type type) {
  return type >= TYPE_MIFARE_CLASSIC_MINI && type <= TYPE_MIFARE_CLASSIC_4K;
}

bool NfcDumpParser::isType2(Type type) {
  return type >= TYPE_NTAG210 && type <= TYPE_NTAG216;
}

NfcDumpParser::Info NfcDumpParser::inspect(const uint8_t* dump, size_t dumpLen) {
  Info info;
  info.size = dumpLen;
  info.type = typeForSize(dumpLen);
  if (!dump || info.type == TYPE_UNKNOWN) return info;

  if (isMifareClassic(info.type)) {
    if (dumpLen < 16) return info;
    const uint8_t bcc4 = dump[0] ^ dump[1] ^ dump[2] ^ dump[3];
    if (dump[4] == bcc4 && (dump[6] & 0xC0) == 0x00) {
      memcpy(info.uid, dump, 4);
      info.uidLen = 4;
      info.uidValid = true;
    } else if ((dump[8] & 0xC0) == 0x40) {
      // This is the 7-byte raw block-0 layout already accepted by the CU
      // loader. It has no standalone BCC byte to validate here.
      memcpy(info.uid, dump, 7);
      info.uidLen = 7;
      info.uidValid = true;
    }
    return info;
  }

  if (isType2(info.type)) {
    if (dumpLen < 12) return info;
    const uint8_t bcc0 = 0x88 ^ dump[0] ^ dump[1] ^ dump[2];
    const uint8_t bcc1 = dump[4] ^ dump[5] ^ dump[6] ^ dump[7];
    if (dump[3] == bcc0 && dump[8] == bcc1) {
      info.uid[0] = dump[0]; info.uid[1] = dump[1]; info.uid[2] = dump[2];
      info.uid[3] = dump[4]; info.uid[4] = dump[5]; info.uid[5] = dump[6];
      info.uid[6] = dump[7];
      info.uidLen = 7;
      info.uidValid = true;
    }
  }
  return info;
}
