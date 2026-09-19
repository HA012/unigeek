#include "HfDumpParser.h"
#include "NdefParser.h"
#include <string.h>

HfDumpParser::Type HfDumpParser::typeForSize(size_t size) {
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

const char* HfDumpParser::typeName(Type type) {
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

bool HfDumpParser::isMifareClassic(Type type) {
  return type >= TYPE_MIFARE_CLASSIC_MINI && type <= TYPE_MIFARE_CLASSIC_4K;
}

bool HfDumpParser::isType2(Type type) {
  return type >= TYPE_NTAG210 && type <= TYPE_NTAG216;
}

size_t HfDumpParser::mifareClassicSectorCount(Type type) {
  switch (type) {
    case TYPE_MIFARE_CLASSIC_MINI: return 5;
    case TYPE_MIFARE_CLASSIC_1K:   return 16;
    case TYPE_MIFARE_CLASSIC_2K:   return 32;
    case TYPE_MIFARE_CLASSIC_4K:   return 40;
    default: return 0;
  }
}


HfDumpParser::PasswordInfo HfDumpParser::inspectPassword(const uint8_t* dump, size_t dumpLen) {
  PasswordInfo info;
  if (!dump) return info;

  const Type type = typeForSize(dumpLen);
  uint16_t config0 = 0xFFFF;
  switch (type) {
    case TYPE_NTAG210: config0 = 16; break;
    case TYPE_NTAG212: config0 = 37; break;
    case TYPE_NTAG213: config0 = 41; break;
    case TYPE_NTAG215: config0 = 131; break;
    case TYPE_NTAG216: config0 = 227; break;
    default: return info;
  }

  const size_t off = (size_t)config0 * 4u;
  if (off + 16u > dumpLen) return info;

  info.supported = true;
  info.auth0 = dump[off + 3u];
  info.access = dump[off + 4u];
  info.protectRead = (info.access & 0x80u) != 0;
  info.configLocked = (info.access & 0x40u) != 0;
  info.authLimit = info.access & 0x07u;
  // AUTH0 protects from the selected page onward. Values outside this raw
  // image (including the factory default FFh) leave protection disabled.
  info.enabled = info.auth0 < (dumpLen / 4u);
  memcpy(info.pwd, &dump[off + 8u], 4u);
  memcpy(info.pack, &dump[off + 12u], 2u);
  return info;
}

bool HfDumpParser::setPassword(uint8_t* dump, size_t dumpLen,
                                    const uint8_t pwd[4], const uint8_t pack[2],
                                    bool protectRead) {
  if (!dump || !pwd || !pack) return false;
  const PasswordInfo current = inspectPassword(dump, dumpLen);
  if (!current.supported || current.configLocked) return false;

  const Type type = typeForSize(dumpLen);
  uint16_t config0 = 0xFFFF;
  switch (type) {
    case TYPE_NTAG210: config0 = 16; break;
    case TYPE_NTAG212: config0 = 37; break;
    case TYPE_NTAG213: config0 = 41; break;
    case TYPE_NTAG215: config0 = 131; break;
    case TYPE_NTAG216: config0 = 227; break;
    default: return false;
  }
  const size_t off = (size_t)config0 * 4u;
  if (off + 16u > dumpLen) return false;

  uint8_t access = dump[off + 4u];
  access = (uint8_t)((access & ~0x87u) | (protectRead ? 0x80u : 0u));
  dump[off + 3u] = 4u;       // AUTH0: protect all user pages
  dump[off + 4u] = access;   // PROT + AUTHLIM=0; preserve other bits
  memcpy(&dump[off + 8u], pwd, 4u);
  memcpy(&dump[off + 12u], pack, 2u);
  return true;
}

bool HfDumpParser::removePassword(uint8_t* dump, size_t dumpLen) {
  if (!dump) return false;
  const PasswordInfo current = inspectPassword(dump, dumpLen);
  if (!current.supported || current.configLocked) return false;

  const Type type = typeForSize(dumpLen);
  uint16_t config0 = 0xFFFF;
  switch (type) {
    case TYPE_NTAG210: config0 = 16; break;
    case TYPE_NTAG212: config0 = 37; break;
    case TYPE_NTAG213: config0 = 41; break;
    case TYPE_NTAG215: config0 = 131; break;
    case TYPE_NTAG216: config0 = 227; break;
    default: return false;
  }
  const size_t off = (size_t)config0 * 4u;
  if (off + 16u > dumpLen) return false;

  // All validation is complete before the first write. Preserve unrelated
  // ACCESS bits while restoring the password fields to NXP delivery values.
  const uint8_t access = (uint8_t)(dump[off + 4u] & ~0x87u);
  dump[off + 3u] = 0xFFu;
  dump[off + 4u] = access;
  memset(&dump[off + 8u], 0xFF, 4u);
  dump[off + 12u] = 0x00u;
  dump[off + 13u] = 0x00u;
  return true;
}

bool HfDumpParser::extractNdef(const uint8_t* dump, size_t dumpLen,
                                uint8_t** ndef, size_t* ndefLen) {
  if (!ndef || !ndefLen) return false;
  *ndef = nullptr;
  *ndefLen = 0;
  if (!dump) return false;

  const Type type = typeForSize(dumpLen);
  if (isMifareClassic(type)) {
    return NdefParser::extractMifareClassicNdef(
        dump, dumpLen, mifareClassicSectorCount(type), ndef, ndefLen);
  }

  if (isType2(type)) {
    const uint8_t* view = nullptr;
    size_t viewLen = 0;
    if (!NdefParser::extractType2Ndef(dump, dumpLen, &view, &viewLen)) return false;

    if (viewLen == 0) {
      // Preserve the API distinction between "no NDEF TLV" (false) and a
      // present but empty NDEF TLV (true) without relying on new[0].
      *ndef = nullptr;
      *ndefLen = 0;
      return true;
    }
    uint8_t* copy = new uint8_t[viewLen];
    if (!copy) return false;
    memcpy(copy, view, viewLen);
    *ndef = copy;
    *ndefLen = viewLen;
    return true;
  }

  return false;
}


namespace {

static size_t _type2NdefCapacity(HfDumpParser::Type type) {
  switch (type) {
    case HfDumpParser::TYPE_NTAG210: return 48;
    case HfDumpParser::TYPE_NTAG212: return 128;
    case HfDumpParser::TYPE_NTAG213: return 144;
    case HfDumpParser::TYPE_NTAG215: return 496;
    case HfDumpParser::TYPE_NTAG216: return 872;
    default: return 0;
  }
}

static bool _classicNdefSectors(const uint8_t* dump, size_t dumpLen,
                                size_t totalSectors, uint8_t* sectors,
                                size_t& sectorCount, size_t& capacity) {
  sectorCount = 0;
  capacity = 0;
  if (!dump || !sectors) return false;
  if (totalSectors != 5 && totalSectors != 16 &&
      totalSectors != 32 && totalSectors != 40) return false;

  const size_t expected = totalSectors == 5 ? 320u :
                          totalSectors == 16 ? 1024u :
                          totalSectors == 32 ? 2048u : 4096u;
  if (dumpLen < expected) return false;

  auto addIfNdef = [&](uint8_t sector, uint8_t lo, uint8_t hi) {
    if (sector >= totalSectors || sectorCount >= 39) return;
    if (lo == 0x03 && hi == 0xE1) {
      sectors[sectorCount++] = sector;
      capacity += (sector < 32) ? 48u : 240u;
    }
  };

  const uint8_t* b1 = &dump[16u];
  const uint8_t* b2 = &dump[32u];
  for (uint8_t sector = 1; sector <= 7 && sector < totalSectors; ++sector) {
    const size_t off = 2u + (size_t)(sector - 1u) * 2u;
    addIfNdef(sector, b1[off], b1[off + 1u]);
  }
  for (uint8_t sector = 8; sector <= 15 && sector < totalSectors; ++sector) {
    const size_t off = (size_t)(sector - 8u) * 2u;
    addIfNdef(sector, b2[off], b2[off + 1u]);
  }

  if (totalSectors > 16 && dumpLen >= 67u * 16u) {
    const uint8_t* m0 = &dump[64u * 16u];
    const uint8_t* m1 = &dump[65u * 16u];
    const uint8_t* m2 = &dump[66u * 16u];
    for (uint8_t sector = 17; sector <= 23 && sector < totalSectors; ++sector) {
      const size_t off = 2u + (size_t)(sector - 17u) * 2u;
      addIfNdef(sector, m0[off], m0[off + 1u]);
    }
    for (uint8_t sector = 24; sector <= 31 && sector < totalSectors; ++sector) {
      const size_t off = (size_t)(sector - 24u) * 2u;
      addIfNdef(sector, m1[off], m1[off + 1u]);
    }
    for (uint8_t sector = 32; sector <= 39 && sector < totalSectors; ++sector) {
      const size_t off = (size_t)(sector - 32u) * 2u;
      addIfNdef(sector, m2[off], m2[off + 1u]);
    }
  }
  return sectorCount > 0;
}

static size_t _tlvSize(size_t ndefLen) {
  return ndefLen + ((ndefLen < 0xFF) ? 3u : 5u);
}

static void _writeTlvByte(uint8_t value, uint8_t* tlv, size_t& pos) {
  tlv[pos++] = value;
}

// Locate the complete NDEF TLV in a bounded TLV area. The returned range
// includes the type and length field but not any following TLVs/terminator.
static bool _findNdefTlv(const uint8_t* area, size_t areaLen,
                         size_t& tlvStart, size_t& valueStart,
                         size_t& valueLen, size_t& tlvEnd) {
  size_t p = 0;
  while (p < areaLen) {
    const size_t start = p;
    const uint8_t type = area[p++];
    if (type == 0x00) continue;
    if (type == 0xFE) return false;
    if (p >= areaLen) return false;

    size_t len = area[p++];
    if (len == 0xFF) {
      if (p + 1 >= areaLen) return false;
      len = ((size_t)area[p] << 8) | area[p + 1];
      p += 2;
    }
    if (len > areaLen - p) return false;

    if (type == 0x03) {
      tlvStart = start;
      valueStart = p;
      valueLen = len;
      tlvEnd = p + len;
      return true;
    }
    p += len;
  }
  return false;
}

// Return the logical end of a TLV stream after `from`. A terminator ends the
// meaningful stream; bytes after it are padding and need not be shifted.
static bool _tlvStreamEnd(const uint8_t* area, size_t areaLen, size_t from,
                          size_t& streamEnd) {
  size_t p = from;
  while (p < areaLen) {
    const uint8_t type = area[p++];
    if (type == 0x00) continue;
    if (type == 0xFE) { streamEnd = p; return true; }
    if (p >= areaLen) return false;
    size_t len = area[p++];
    if (len == 0xFF) {
      if (p + 1 >= areaLen) return false;
      len = ((size_t)area[p] << 8) | area[p + 1];
      p += 2;
    }
    if (len > areaLen - p) return false;
    p += len;
  }
  streamEnd = areaLen;
  return true;
}

static void _writeNdefTlv(uint8_t* out, size_t& pos,
                          const uint8_t* ndef, size_t ndefLen) {
  _writeTlvByte(0x03, out, pos);
  if (ndefLen < 0xFF) {
    _writeTlvByte((uint8_t)ndefLen, out, pos);
  } else {
    _writeTlvByte(0xFF, out, pos);
    _writeTlvByte((uint8_t)((ndefLen >> 8) & 0xFF), out, pos);
    _writeTlvByte((uint8_t)(ndefLen & 0xFF), out, pos);
  }
  if (ndefLen) { memcpy(out + pos, ndef, ndefLen); pos += ndefLen; }
}

static bool _collectClassicArea(const uint8_t* dump, const uint8_t* sectors,
                                size_t sectorCount, uint8_t* area) {
  if (!dump || !sectors || !area) return false;
  size_t out = 0;
  for (size_t i = 0; i < sectorCount; ++i) {
    const uint8_t sector = sectors[i];
    const size_t firstBlock = (sector < 32)
        ? (size_t)sector * 4u
        : 128u + (size_t)(sector - 32u) * 16u;
    const uint8_t dataBlocks = (sector < 32) ? 3 : 15;
    for (uint8_t bi = 0; bi < dataBlocks; ++bi) {
      memcpy(area + out, &dump[(firstBlock + bi) * 16u], 16u);
      out += 16u;
    }
  }
  return true;
}

static void _writeClassicArea(uint8_t* dump, const uint8_t* sectors,
                              size_t sectorCount, const uint8_t* area) {
  size_t src = 0;
  for (size_t i = 0; i < sectorCount; ++i) {
    const uint8_t sector = sectors[i];
    const size_t firstBlock = (sector < 32)
        ? (size_t)sector * 4u
        : 128u + (size_t)(sector - 32u) * 16u;
    const uint8_t dataBlocks = (sector < 32) ? 3 : 15;
    for (uint8_t bi = 0; bi < dataBlocks; ++bi) {
      memcpy(&dump[(firstBlock + bi) * 16u], area + src, 16u);
      src += 16u;
    }
  }
}

} // namespace

bool HfDumpParser::replaceNdef(uint8_t* dump, size_t dumpLen,
                                const uint8_t* ndef, size_t ndefLen) {
  if (!dump || (ndefLen > 0 && !ndef)) return false;
  const Type type = typeForSize(dumpLen);
  if (type == TYPE_UNKNOWN) return false;

  const size_t newTlvLen = _tlvSize(ndefLen) - 1u; // excludes terminator

  if (isType2(type)) {
    const size_t modelCapacity = _type2NdefCapacity(type);
    if (modelCapacity == 0 || dumpLen < 16u) return false;
    if (dump[12] != 0xE1 || dump[13] != 0x10) return false;

    const size_t capacity = (size_t)dump[14] * 8u;
    if (capacity == 0 || capacity > modelCapacity || capacity > dumpLen - 16u)
      return false;

    size_t oldStart = 0, oldValue = 0, oldLen = 0, oldEnd = 0;
    if (!_findNdefTlv(&dump[16], capacity, oldStart, oldValue, oldLen, oldEnd))
      return false;

    size_t streamEnd = 0;
    if (!_tlvStreamEnd(&dump[16], capacity, oldEnd, streamEnd)) return false;
    const size_t suffixLen = streamEnd - oldEnd;
    if (newTlvLen > capacity - oldStart ||
        suffixLen > capacity - oldStart - newTlvLen) return false;

    // Build the complete advertised data area first. This preserves NULL,
    // Lock/Memory Control, Proprietary and terminator TLVs before/after NDEF
    // and keeps the caller's dump unchanged on every failure path.
    uint8_t* area = new uint8_t[capacity];
    if (!area) return false;
    memcpy(area, &dump[16], oldStart);
    size_t pos = oldStart;
    _writeNdefTlv(area, pos, ndef, ndefLen);
    memcpy(area + pos, &dump[16 + oldEnd], suffixLen);
    pos += suffixLen;
    if (pos < capacity) memset(area + pos, 0x00, capacity - pos);

    memcpy(&dump[16], area, capacity);
    delete[] area;
    (void)oldValue;
    (void)oldLen;
    return true;
  }

  if (isMifareClassic(type)) {
    uint8_t sectors[39] = {};
    size_t sectorCount = 0, capacity = 0;
    if (!_classicNdefSectors(dump, dumpLen, mifareClassicSectorCount(type),
                             sectors, sectorCount, capacity)) return false;

    uint8_t* oldArea = new uint8_t[capacity];
    if (!oldArea) return false;
    _collectClassicArea(dump, sectors, sectorCount, oldArea);

    size_t oldStart = 0, oldValue = 0, oldLen = 0, oldEnd = 0;
    if (!_findNdefTlv(oldArea, capacity, oldStart, oldValue, oldLen, oldEnd)) {
      delete[] oldArea;
      return false;
    }

    size_t streamEnd = 0;
    if (!_tlvStreamEnd(oldArea, capacity, oldEnd, streamEnd)) {
      delete[] oldArea;
      return false;
    }
    const size_t suffixLen = streamEnd - oldEnd;
    if (newTlvLen > capacity - oldStart ||
        suffixLen > capacity - oldStart - newTlvLen) {
      delete[] oldArea;
      return false;
    }

    uint8_t* newArea = new uint8_t[capacity];
    if (!newArea) { delete[] oldArea; return false; }
    memcpy(newArea, oldArea, oldStart);
    size_t pos = oldStart;
    _writeNdefTlv(newArea, pos, ndef, ndefLen);
    memcpy(newArea + pos, oldArea + oldEnd, suffixLen);
    pos += suffixLen;
    if (pos < capacity) memset(newArea + pos, 0x00, capacity - pos);

    _writeClassicArea(dump, sectors, sectorCount, newArea);
    delete[] newArea;
    delete[] oldArea;
    (void)oldValue;
    (void)oldLen;
    return true;
  }

  return false;
}

bool HfDumpParser::setUid(uint8_t* dump, size_t dumpLen,
                           const uint8_t* uid, size_t uidLen) {
  if (!dump || !uid) return false;

  const Info info = inspect(dump, dumpLen);
  if (!info.uidValid) return false;

  if (isMifareClassic(info.type)) {
    // Only the conventional 4-byte Classic manufacturer-block layout is
    // writable here. The 7-byte CU layout remains intentionally read-only.
    if (info.uidLen != 4 || uidLen != 4 || dumpLen < 5) return false;

    const uint8_t bcc = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
    memcpy(dump, uid, 4);
    dump[4] = bcc;
    return true;
  }

  if (isType2(info.type)) {
    if (info.uidLen != 7 || uidLen != 7 || dumpLen < 9) return false;

    const uint8_t bcc0 = 0x88 ^ uid[0] ^ uid[1] ^ uid[2];
    const uint8_t bcc1 = uid[3] ^ uid[4] ^ uid[5] ^ uid[6];

    dump[0] = uid[0];
    dump[1] = uid[1];
    dump[2] = uid[2];
    dump[3] = bcc0;
    dump[4] = uid[3];
    dump[5] = uid[4];
    dump[6] = uid[5];
    dump[7] = uid[6];
    dump[8] = bcc1;
    return true;
  }

  return false;
}

HfDumpParser::Info HfDumpParser::inspect(const uint8_t* dump, size_t dumpLen) {
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

    uint8_t* ndef = nullptr;
    size_t ndefLen = 0;
    if (extractNdef(dump, dumpLen, &ndef, &ndefLen)) {
      info.hasNdef = true;
      info.ndefLen = ndefLen;
    }
    delete[] ndef;
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

    uint8_t* ndef = nullptr;
    size_t ndefLen = 0;
    if (extractNdef(dump, dumpLen, &ndef, &ndefLen)) {
      info.hasNdef = true;
      info.ndefLen = ndefLen;
    }
    delete[] ndef;
  }
  return info;
}
