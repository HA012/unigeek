#include "utils/IdentityFile.h"
#include "core/Device.h"

namespace {
String spacedHex(const uint8_t* data, size_t len) {
  String out;
  out.reserve(len ? len * 3u - 1u : 0u);
  char b[3];
  for (size_t i = 0; i < len; ++i) {
    if (i) out += ' ';
    snprintf(b, sizeof(b), "%02X", data[i]);
    out += b;
  }
  return out;
}
}  // namespace

namespace IdentityFile {

bool writeTextFile(const String& path, const String& content) {
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) return false;
  const size_t expected = content.length();
  const size_t written = f.write(
      reinterpret_cast<const uint8_t*>(content.c_str()), expected);
  f.close();
  return written == expected;
}

bool saveNfcUid(const String& path, const uint8_t* uid, size_t uidLen) {
  if (!uid || !uidLen || !Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String content;
  content.reserve(96 + uidLen * 3u);
  content += "Filetype: UniGeek NFC UID\n";
  content += "Version: 1\n";
  content += "Protocol: ISO14443A\n";
  content += "UID: ";
  content += spacedHex(uid, uidLen);
  content += '\n';
  return writeTextFile(path, content);
}

bool saveLfId(const String& path, LFCodec::Protocol protocol,
              const uint8_t* data, size_t dataLen) {
  if (!data || !dataLen || !Uni.Storage || !Uni.Storage->isAvailable()) return false;
  LFCodec::DecodedData decoded;
  if (!LFCodec::decode(protocol, data, dataLen, decoded)) return false;
  const LFCodec::FormatInfo* info = LFCodec::format(protocol);
  if (!info) return false;

  String content;
  content.reserve(160 + dataLen * 3u);
  content += "Filetype: UniGeek RFID ID\n";
  content += "Version: 1\n";
  content += "Protocol: ";
  content += info->name;
  content += '\n';

  LFCodec::Field fields[4];
  const size_t count = LFCodec::fields(decoded, fields, 4);
  for (size_t i = 0; i < count; ++i) {
    // Data is emitted once, below, in the canonical lossless representation.
    if (strcmp(fields[i].label, "Data") == 0) continue;
    content += fields[i].label;
    content += ": ";
    content += fields[i].value;
    content += '\n';
  }
  content += "Data: ";
  content += spacedHex(data, dataLen);
  content += '\n';
  return writeTextFile(path, content);
}

}  // namespace IdentityFile
