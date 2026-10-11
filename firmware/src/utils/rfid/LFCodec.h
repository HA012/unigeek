#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace LFCodec {

enum class Protocol : uint8_t {
  Unknown = 0,
  EM410X,
  HIDProx,
  IoProx,
  Viking,
  PACStanley,
  Jablotron,
};

static constexpr uint8_t kMaxDataSize = 16;

enum Capability : uint8_t {
  CanWriteT5577 = 1 << 0,
  CanGenerateRandom = 1 << 1,
};

enum class FieldId : uint8_t {
  NumericId,
  FacilityCode,
  CardNumber,
  TextId,
  RawData,
};

enum class FieldType : uint8_t {
  Hex,
  Decimal,
  Text,
};

struct FieldInfo {
  FieldId id;
  const char* label;
  FieldType type;
};

struct FormatInfo {
  Protocol protocol;
  uint16_t chameleonType;
  const char* name;
  const char* filePrefix;
  uint8_t dataSize;
  uint8_t capabilities;
};

struct DecodedData {
  Protocol protocol = Protocol::Unknown;
  uint8_t data[kMaxDataSize] = {};
  uint8_t length = 0;

  bool hasNumericId = false;
  uint64_t numericId = 0;

  bool hasFacilityCode = false;
  uint32_t facilityCode = 0;
  bool hasCardNumber = false;
  uint32_t cardNumber = 0;

  bool hasTextId = false;
  char textId[kMaxDataSize + 1] = {};
};

size_t formatCount();
const FormatInfo* formatAt(size_t index);
const FormatInfo* format(Protocol protocol);
const FormatInfo* fromChameleonType(uint16_t type);
const FormatInfo* fromFilename(const String& path);
bool validate(Protocol protocol, size_t length);
bool isSupportedT5577(uint16_t chameleonType);

bool create(Protocol protocol, DecodedData& out);

size_t editableFieldCount(Protocol protocol);
const FieldInfo* editableFieldAt(Protocol protocol, size_t index);

bool setField(DecodedData& data, FieldId id, const String& value);

bool decode(Protocol protocol, const uint8_t* data, size_t length, DecodedData& out);

bool isComplete(const DecodedData& decoded);

bool encode(const DecodedData& decoded, uint8_t* out, size_t outSize);

struct Field {
  const char* label;
  String value;
};

size_t fields(const DecodedData& decoded, Field* out, size_t capacity);

String hex(const uint8_t* data, size_t length, bool colonSeparated = false);

}
