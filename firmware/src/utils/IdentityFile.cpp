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

namespace {
bool readTextFile(const String& path, String& out) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "r");
  if (!f) return false;
  out = "";
  while (f.available()) out += (char)f.read();
  f.close();
  return out.length() > 0;
}
String valueFor(const String& text, const char* key) {
  String prefix = String(key) + ":";
  int start = 0;
  while (start < (int)text.length()) {
    int end = text.indexOf('\n', start); if (end < 0) end = text.length();
    String line = text.substring(start, end); line.trim();
    if (line.startsWith(prefix)) { String v=line.substring(prefix.length()); v.trim(); return v; }
    start = end + 1;
  }
  return "";
}
bool hexBytes(const String& text, uint8_t* out, size_t cap, size_t& len) {
  String h;
  for (size_t i=0;i<text.length();++i) if (isxdigit((unsigned char)text[i])) h += text[i];
  if (!h.length() || (h.length() & 1)) return false;
  len = h.length()/2; if (len > cap) return false;
  for (size_t i=0;i<len;++i) { char b[3]={h[i*2],h[i*2+1],0}; char* e=nullptr; out[i]=(uint8_t)strtoul(b,&e,16); if(!e||*e)return false; }
  return true;
}
}

bool loadNfcUid(const String& path, uint8_t* uid, size_t capacity, size_t& uidLen) {
  uidLen=0; if(!uid||!capacity)return false; String t; if(!readTextFile(path,t))return false;
  if(valueFor(t,"Filetype")!="UniGeek NFC UID" || valueFor(t,"Version")!="1" || valueFor(t,"Protocol")!="ISO14443A") return false;
  if(!hexBytes(valueFor(t,"UID"),uid,capacity,uidLen))return false;
  return uidLen==4 || uidLen==7 || uidLen==10;
}

bool loadLfId(const String& path, LFCodec::DecodedData& out) {
  String t; if(!readTextFile(path,t))return false;
  if(valueFor(t,"Filetype")!="UniGeek RFID ID" || valueFor(t,"Version")!="1")return false;
  String proto=valueFor(t,"Protocol"); LFCodec::Protocol p=LFCodec::Protocol::Unknown;
  for(size_t i=0;i<LFCodec::formatCount();++i){auto f=LFCodec::formatAt(i);if(f&&proto==f->name){p=f->protocol;break;}}
  auto info=LFCodec::format(p); if(!info)return false;
  uint8_t raw[LFCodec::kMaxDataSize]={}; size_t n=0; if(!hexBytes(valueFor(t,"Data"),raw,sizeof(raw),n)||n!=info->dataSize)return false;
  return LFCodec::decode(p,raw,n,out);
}

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

bool loadNfcUid(const String& path, uint8_t* uid, size_t capacity, size_t& uidLen) {
  uidLen = 0;
  if (!uid || capacity < 4 || !Uni.Storage || !Uni.Storage->isAvailable()) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "r");
  if (!f) return false;
  String text;
  while (f.available()) text += (char)f.read();
  f.close();
  const int pos = text.indexOf("UID:");
  if (pos < 0) return false;
  int end = text.indexOf('\n', pos);
  String hex = text.substring(pos + 4, end < 0 ? text.length() : end);
  hex.trim(); hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length() != 8 && hex.length() != 14) return false;
  const size_t n = hex.length() / 2u;
  if (n > capacity) return false;
  for (size_t i = 0; i < n; ++i) {
    char b[3] = {hex[i*2], hex[i*2+1], 0}; char* e = nullptr;
    unsigned long v = strtoul(b, &e, 16); if (!e || *e) return false; uid[i] = (uint8_t)v;
  }
  uidLen = n; return true;
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
