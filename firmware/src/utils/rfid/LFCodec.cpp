#include "LFCodec.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

namespace LFCodec {
namespace {

static const FormatInfo kFormats[] = {
    {Protocol::EM410X,     100, "EM410X",      "EM410X",       5, CanWriteT5577},
    {Protocol::HIDProx,    200, "HID Prox",    "HID-Prox",     13, CanWriteT5577},
    {Protocol::IoProx,     201, "ioProx",      "ioProx",       16, CanWriteT5577},
    {Protocol::Viking,     170, "Viking",      "Viking",        4, CanWriteT5577},
    {Protocol::PACStanley, 150, "PAC/Stanley", "PAC-Stanley",   8, CanWriteT5577},
    {Protocol::Jablotron,  180, "Jablotron",   "Jablotron",     5, CanWriteT5577},
};

constexpr size_t kFormatCount = sizeof(kFormats) / sizeof(kFormats[0]);

static const FieldInfo kEM410XFields[] = {
    {FieldId::NumericId, "UID", FieldType::Hex},
};
static const FieldInfo kHIDProxFields[] = {
    {FieldId::RawData, "Data", FieldType::Hex},
};
static const FieldInfo kIoProxFields[] = {
    {FieldId::FacilityCode, "Facility", FieldType::Decimal},
    {FieldId::CardNumber, "Card Number", FieldType::Decimal},
};
static const FieldInfo kVikingFields[] = {
    {FieldId::RawData, "UID", FieldType::Hex},
};
static const FieldInfo kPACStanleyFields[] = {
    {FieldId::TextId, "ID", FieldType::Text},
};
static const FieldInfo kJablotronFields[] = {
    {FieldId::RawData, "ID", FieldType::Hex},
};

template <size_t N>
const FieldInfo* fieldAt(const FieldInfo (&fields)[N], size_t index) {
  return index < N ? &fields[index] : nullptr;
}

template <size_t N>
constexpr size_t fieldCount(const FieldInfo (&)[N]) { return N; }

bool parseDecimal(const String& value, uint32_t maxValue, uint32_t& out) {
  String s = value;
  s.trim();
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); ++i) {
    if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  char* end = nullptr;
  const unsigned long parsed = strtoul(s.c_str(), &end, 10);
  if (!end || *end != '\0' || parsed > maxValue) return false;
  out = static_cast<uint32_t>(parsed);
  return true;
}

bool parseHexBytes(const String& value, uint8_t* out, size_t expected) {
  if (!out || !expected) return false;
  String compact;
  compact.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == ':' || c == '-' || isspace(static_cast<unsigned char>(c))) continue;
    if (!isxdigit(static_cast<unsigned char>(c))) return false;
    compact += c;
  }
  if (compact.length() != expected * 2) return false;
  for (size_t i = 0; i < expected; ++i) {
    char pair[3] = {compact[i * 2], compact[i * 2 + 1], '\0'};
    out[i] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
  }
  return true;
}

}  // namespace

size_t formatCount() { return kFormatCount; }

const FormatInfo* formatAt(size_t index) {
  return index < kFormatCount ? &kFormats[index] : nullptr;
}

const FormatInfo* format(Protocol protocol) {
  for (size_t i = 0; i < kFormatCount; ++i) {
    if (kFormats[i].protocol == protocol) return &kFormats[i];
  }
  return nullptr;
}

const FormatInfo* fromChameleonType(uint16_t type) {
  for (size_t i = 0; i < kFormatCount; ++i) {
    if (kFormats[i].chameleonType == type) return &kFormats[i];
  }
  return nullptr;
}

const FormatInfo* fromFilename(const String& path) {
  const int slash = path.lastIndexOf('/');
  const String name = slash >= 0 ? path.substring(slash + 1) : path;
  for (size_t i = 0; i < kFormatCount; ++i) {
    const String prefix = String(kFormats[i].filePrefix) + "_";
    // Canonical LF files are <prefix>_<identifier>.bin.  Keep protocol
    // detection strict so unrelated files with a familiar prefix are ignored.
    if (name.startsWith(prefix) && name.endsWith(".bin") &&
        name.length() > prefix.length() + 4) return &kFormats[i];
  }
  return nullptr;
}

bool validate(Protocol protocol, size_t length) {
  const FormatInfo* info = format(protocol);
  return info && length == info->dataSize;
}

bool isSupportedT5577(uint16_t chameleonType) {
  const FormatInfo* info = fromChameleonType(chameleonType);
  return info && (info->capabilities & CanWriteT5577);
}

bool create(Protocol protocol, DecodedData& out) {
  out = DecodedData{};
  const FormatInfo* info = format(protocol);
  if (!info || info->dataSize > kMaxDataSize) return false;

  out.protocol = protocol;
  out.length = info->dataSize;

  switch (protocol) {
    case Protocol::EM410X:
      out.hasNumericId = true;
      break;
    case Protocol::IoProx:
      out.hasFacilityCode = true;
      out.hasCardNumber = true;
      break;
    case Protocol::PACStanley:
      // Canonical bytes start at zero. A printable text ID becomes present
      // only after setField(TextId, ...) supplies a valid eight-character ID.
      break;
    case Protocol::HIDProx:
    case Protocol::Viking:
    case Protocol::Jablotron:
      // These formats are currently edited as exact-size canonical hex data.
      break;
    default:
      out = DecodedData{};
      return false;
  }
  return true;
}

size_t editableFieldCount(Protocol protocol) {
  switch (protocol) {
    case Protocol::EM410X: return fieldCount(kEM410XFields);
    case Protocol::HIDProx: return fieldCount(kHIDProxFields);
    case Protocol::IoProx: return fieldCount(kIoProxFields);
    case Protocol::Viking: return fieldCount(kVikingFields);
    case Protocol::PACStanley: return fieldCount(kPACStanleyFields);
    case Protocol::Jablotron: return fieldCount(kJablotronFields);
    default: return 0;
  }
}

const FieldInfo* editableFieldAt(Protocol protocol, size_t index) {
  switch (protocol) {
    case Protocol::EM410X: return fieldAt(kEM410XFields, index);
    case Protocol::HIDProx: return fieldAt(kHIDProxFields, index);
    case Protocol::IoProx: return fieldAt(kIoProxFields, index);
    case Protocol::Viking: return fieldAt(kVikingFields, index);
    case Protocol::PACStanley: return fieldAt(kPACStanleyFields, index);
    case Protocol::Jablotron: return fieldAt(kJablotronFields, index);
    default: return nullptr;
  }
}

bool setField(DecodedData& data, FieldId id, const String& value) {
  const FormatInfo* info = format(data.protocol);
  if (!info || data.length != info->dataSize) return false;

  switch (id) {
    case FieldId::NumericId: {
      if (data.protocol != Protocol::EM410X) return false;
      uint8_t bytes[5] = {};
      if (!parseHexBytes(value, bytes, sizeof(bytes))) return false;
      uint64_t numeric = 0;
      for (uint8_t byte : bytes) numeric = (numeric << 8) | byte;
      data.numericId = numeric;
      data.hasNumericId = true;
      memcpy(data.data, bytes, sizeof(bytes));
      return true;
    }

    case FieldId::FacilityCode: {
      if (data.protocol != Protocol::IoProx) return false;
      uint32_t parsed = 0;
      if (!parseDecimal(value, 0xFF, parsed)) return false;
      data.facilityCode = parsed;
      data.hasFacilityCode = true;
      data.data[1] = static_cast<uint8_t>(parsed);
      return true;
    }

    case FieldId::CardNumber: {
      if (data.protocol != Protocol::IoProx) return false;
      uint32_t parsed = 0;
      if (!parseDecimal(value, 0xFFFF, parsed)) return false;
      data.cardNumber = parsed;
      data.hasCardNumber = true;
      data.data[2] = static_cast<uint8_t>(parsed >> 8);
      data.data[3] = static_cast<uint8_t>(parsed);
      return true;
    }

    case FieldId::TextId: {
      if (data.protocol != Protocol::PACStanley || value.length() != info->dataSize) return false;
      for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        if (c < 0x20 || c > 0x7E) return false;
      }
      memset(data.textId, 0, sizeof(data.textId));
      memcpy(data.textId, value.c_str(), info->dataSize);
      data.hasTextId = true;
      memcpy(data.data, value.c_str(), info->dataSize);
      return true;
    }

    case FieldId::RawData: {
      if (data.protocol != Protocol::HIDProx && data.protocol != Protocol::Viking &&
          data.protocol != Protocol::Jablotron) return false;
      uint8_t bytes[kMaxDataSize] = {};
      if (!parseHexBytes(value, bytes, info->dataSize)) return false;
      memcpy(data.data, bytes, info->dataSize);
      return true;
    }
  }
  return false;
}

bool encode(const DecodedData& decoded, uint8_t* out, size_t outSize) {
  const FormatInfo* info = format(decoded.protocol);
  if (!info || !out || decoded.length != info->dataSize || outSize < info->dataSize) return false;

  // Start from the canonical bytes captured by decode(). This is deliberate:
  // protocols with only partially understood semantics retain every opaque bit.
  memcpy(out, decoded.data, info->dataSize);

  switch (decoded.protocol) {
    case Protocol::EM410X: {
      if (!decoded.hasNumericId || decoded.numericId > 0xFFFFFFFFFFULL) return false;
      uint64_t value = decoded.numericId;
      for (int i = info->dataSize - 1; i >= 0; --i) {
        out[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
      }
      break;
    }

    case Protocol::IoProx:
      if (!decoded.hasFacilityCode || decoded.facilityCode > 0xFF ||
          !decoded.hasCardNumber || decoded.cardNumber > 0xFFFF) {
        return false;
      }
      out[1] = static_cast<uint8_t>(decoded.facilityCode);
      out[2] = static_cast<uint8_t>(decoded.cardNumber >> 8);
      out[3] = static_cast<uint8_t>(decoded.cardNumber);
      break;

    case Protocol::PACStanley:
      if (decoded.hasTextId) {
        const size_t textLen = strnlen(decoded.textId, sizeof(decoded.textId));
        if (textLen != info->dataSize) return false;
        for (size_t i = 0; i < textLen; ++i) {
          const uint8_t c = static_cast<uint8_t>(decoded.textId[i]);
          if (c < 0x20 || c > 0x7E) return false;
          out[i] = c;
        }
      }
      break;

    case Protocol::HIDProx:
    case Protocol::Viking:
    case Protocol::Jablotron:
      // No field-level encoder yet; canonical bytes are preserved verbatim.
      break;

    default:
      return false;
  }

  return true;
}

size_t fields(const DecodedData& decoded, Field* out, size_t capacity) {
  if (!out || !capacity || !format(decoded.protocol) ||
      !validate(decoded.protocol, decoded.length)) return 0;

  size_t count = 0;
  auto add = [&](const char* label, const String& value) {
    if (count < capacity) out[count++] = {label, value};
  };
  const String rawHex = hex(decoded.data, decoded.length);

  switch (decoded.protocol) {
    case Protocol::EM410X:
      add("UID (Hex)", hex(decoded.data, decoded.length, true));
      if (decoded.hasNumericId) add("UID (Dec)", String((unsigned long long)decoded.numericId));
      break;
    case Protocol::HIDProx:
      add("Data", rawHex);
      break;
    case Protocol::IoProx:
      if (decoded.hasFacilityCode) add("Facility", String(decoded.facilityCode));
      if (decoded.hasCardNumber) add("Card Number", String(decoded.cardNumber));
      break;
    case Protocol::Viking:
      add("UID", rawHex);
      break;
    case Protocol::PACStanley:
      add(decoded.hasTextId ? "ID" : "Data", decoded.hasTextId ? String(decoded.textId) : rawHex);
      break;
    case Protocol::Jablotron:
      add("ID", rawHex);
      break;
    default:
      break;
  }
  return count;
}

String hex(const uint8_t* data, size_t length, bool colonSeparated) {
  String out;
  if (!data || !length) return out;
  out.reserve(length * (colonSeparated ? 3 : 2));
  char byteHex[3];
  for (size_t i = 0; i < length; ++i) {
    if (colonSeparated && i) out += ':';
    snprintf(byteHex, sizeof(byteHex), "%02X", data[i]);
    out += byteHex;
  }
  return out;
}

bool decode(Protocol protocol, const uint8_t* data, size_t length, DecodedData& out) {
  out = DecodedData{};
  if (!data || !validate(protocol, length) || length > kMaxDataSize) return false;

  out.protocol = protocol;
  out.length = static_cast<uint8_t>(length);
  memcpy(out.data, data, length);

  switch (protocol) {
    case Protocol::EM410X:
      out.hasNumericId = true;
      for (size_t i = 0; i < length; ++i) {
        out.numericId = (out.numericId << 8) | data[i];
      }
      break;

    case Protocol::IoProx:
      // Preserve the interpretation already used by Read Tag: byte 1 is the
      // facility code and bytes 2-3 are the card number.
      out.hasFacilityCode = true;
      out.facilityCode = data[1];
      out.hasCardNumber = true;
      out.cardNumber = (uint16_t(data[2]) << 8) | data[3];
      break;

    case Protocol::PACStanley: {
      bool printable = length > 0;
      for (size_t i = 0; i < length; ++i) {
        if (data[i] < 0x20 || data[i] > 0x7E) {
          printable = false;
          break;
        }
      }
      if (printable) {
        out.hasTextId = true;
        memcpy(out.textId, data, length);
        out.textId[length] = '\0';
      }
      break;
    }

    case Protocol::HIDProx:
    case Protocol::Viking:
    case Protocol::Jablotron:
      // The current firmware presents these as canonical bytes only. Keep
      // that behavior until their field-level semantics are explicitly added.
      break;

    default:
      return false;
  }

  return true;
}

}  // namespace LFCodec
