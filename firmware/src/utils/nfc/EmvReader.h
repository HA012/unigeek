#pragma once
#include <Arduino.h>
#include <string.h>

namespace EmvReader {

static constexpr uint8_t kMaxApps = 8;

struct Application {
  uint8_t aid[16] = {};
  uint8_t aidLen = 0;
  String label;
  uint8_t priority = 0;
};

struct SelectedApplication {
  String label;
  String preferredName;
  uint8_t pdol[64] = {};
  uint8_t pdolLen = 0;
};

inline bool readLength(const uint8_t* data, size_t len, size_t& pos, size_t& outLen) {
  if (pos >= len) return false;
  uint8_t b = data[pos++];
  if ((b & 0x80) == 0) { outLen = b; return pos + outLen <= len; }
  uint8_t n = b & 0x7F;
  if (n == 0 || n > 2 || pos + n > len) return false;
  outLen = 0;
  while (n--) outLen = (outLen << 8) | data[pos++];
  return pos + outLen <= len;
}

inline bool readTag(const uint8_t* data, size_t len, size_t& pos, uint32_t& tag) {
  if (pos >= len) return false;
  tag = data[pos++];
  if ((tag & 0x1F) != 0x1F) return true;
  uint8_t count = 0;
  while (pos < len && count++ < 3) {
    uint8_t b = data[pos++];
    tag = (tag << 8) | b;
    if ((b & 0x80) == 0) return true;
  }
  return false;
}

inline String hex(const uint8_t* data, size_t len) {
  String s;
  for (size_t i = 0; i < len; ++i) {
    char b[3];
    snprintf(b, sizeof(b), "%02X", data[i]);
    s += b;
  }
  return s;
}

inline String ascii(const uint8_t* data, size_t len) {
  String s;
  for (size_t i = 0; i < len; ++i) {
    char c = (char)data[i];
    if (c >= 0x20 && c <= 0x7E) s += c;
  }
  s.trim();
  return s;
}

inline void parseAppTemplate(const uint8_t* data, size_t len, Application& app) {
  size_t pos = 0;
  while (pos < len) {
    uint32_t tag = 0; size_t l = 0;
    if (!readTag(data, len, pos, tag) || !readLength(data, len, pos, l)) return;
    const uint8_t* v = data + pos;
    if (tag == 0x4F && l > 0 && l <= sizeof(app.aid)) {
      memcpy(app.aid, v, l); app.aidLen = (uint8_t)l;
    } else if ((tag == 0x50 || tag == 0x9F12) && app.label.length() == 0) {
      app.label = ascii(v, l);
    } else if (tag == 0x87 && l >= 1) {
      app.priority = v[0];
    } else if (tag == 0x6F || tag == 0xA5 || tag == 0xBF0C || tag == 0x61) {
      parseAppTemplate(v, l, app);
    }
    pos += l;
  }
}

inline void scanTemplates(const uint8_t* data, size_t len, Application* apps, uint8_t& count, uint8_t maxApps) {
  size_t pos = 0;
  while (pos < len && count < maxApps) {
    uint32_t tag = 0; size_t l = 0;
    if (!readTag(data, len, pos, tag) || !readLength(data, len, pos, l)) return;
    const uint8_t* v = data + pos;
    if (tag == 0x61) {
      Application app;
      parseAppTemplate(v, l, app);
      if (app.aidLen) apps[count++] = app;
    } else if (tag == 0x6F || tag == 0xA5 || tag == 0xBF0C) {
      scanTemplates(v, l, apps, count, maxApps);
    }
    pos += l;
  }
}

inline uint8_t parsePpse(const uint8_t* response, size_t len, Application* apps, uint8_t maxApps) {
  if (!response || len < 2 || response[len - 2] != 0x90 || response[len - 1] != 0x00) return 0;
  uint8_t count = 0;
  scanTemplates(response, len - 2, apps, count, maxApps);
  return count;
}

inline void parseSelectedTemplate(const uint8_t* data, size_t len, SelectedApplication& app) {
  size_t pos = 0;
  while (pos < len) {
    uint32_t tag = 0; size_t l = 0;
    if (!readTag(data, len, pos, tag) || !readLength(data, len, pos, l)) return;
    const uint8_t* v = data + pos;
    if (tag == 0x50 && app.label.length() == 0) {
      app.label = ascii(v, l);
    } else if (tag == 0x9F12 && app.preferredName.length() == 0) {
      app.preferredName = ascii(v, l);
    } else if (tag == 0x9F38 && l > 0) {
      const size_t n = l < sizeof(app.pdol) ? l : sizeof(app.pdol);
      memcpy(app.pdol, v, n);
      app.pdolLen = (uint8_t)n;
    } else if ((tag & 0x20) != 0) {
      parseSelectedTemplate(v, l, app);
    }
    pos += l;
  }
}

inline bool parseSelectAid(const uint8_t* response, size_t len, SelectedApplication& app) {
  if (!response || len < 2 || response[len - 2] != 0x90 || response[len - 1] != 0x00) return false;
  parseSelectedTemplate(response, len - 2, app);
  return true;
}

struct GpoResult {
  uint8_t aip[2] = {};
  uint8_t afl[64] = {};
  uint8_t aflLen = 0;
};

struct CardData { String pan; String expiry; };

inline size_t appendDolValue(uint32_t tag, uint8_t len, uint8_t* out, size_t pos, size_t maxLen) {
  if (pos + len > maxLen) return 0;
  memset(out + pos, 0, len);
  if (tag == 0x9F66 && len >= 4) out[pos] = 0x26;
  else if (tag == 0x9F1A && len >= 2) { out[pos + len - 2] = 0x00; out[pos + len - 1] = 0x76; }
  else if (tag == 0x5F2A && len >= 2) { out[pos + len - 2] = 0x09; out[pos + len - 1] = 0x86; }
  else if (tag == 0x9C && len >= 1) out[pos] = 0x00;
  else if (tag == 0x9F37) {
    uint32_t v = (uint32_t)micros() ^ (uint32_t)millis();
    for (uint8_t i = 0; i < len && i < 4; ++i) out[pos + i] = (uint8_t)(v >> (24 - 8 * i));
  }
  return pos + len;
}

inline size_t buildGpo(const SelectedApplication& app, uint8_t* out, size_t maxLen) {
  uint8_t dol[128] = {}; size_t dolLen = 0, pos = 0;
  while (pos < app.pdolLen) {
    uint32_t tag = 0;
    if (!readTag(app.pdol, app.pdolLen, pos, tag) || pos >= app.pdolLen) return 0;
    const uint8_t valueLen = app.pdol[pos++];
    const size_t next = appendDolValue(tag, valueLen, dol, dolLen, sizeof(dol));
    if (!next) return 0;
    dolLen = next;
  }
  if (dolLen > 0x7F || maxLen < dolLen + 8) return 0;
  out[0]=0x80; out[1]=0xA8; out[2]=0x00; out[3]=0x00; out[4]=(uint8_t)(dolLen+2);
  out[5]=0x83; out[6]=(uint8_t)dolLen; if (dolLen) memcpy(out+7,dol,dolLen); out[7+dolLen]=0x00;
  return dolLen+8;
}

inline void findGpoTags(const uint8_t* data, size_t len, GpoResult& result) {
  size_t pos=0;
  while (pos<len) {
    uint32_t tag=0; size_t l=0;
    if (!readTag(data,len,pos,tag) || !readLength(data,len,pos,l)) return;
    const uint8_t* v=data+pos;
    if (tag==0x82 && l>=2) { result.aip[0]=v[0]; result.aip[1]=v[1]; }
    else if (tag==0x94 && l>=4) { size_t n=l<sizeof(result.afl)?l:sizeof(result.afl); n-=n%4; memcpy(result.afl,v,n); result.aflLen=(uint8_t)n; }
    else if ((tag&0x20)!=0) findGpoTags(v,l,result);
    pos+=l;
  }
}

inline bool parseGpo(const uint8_t* response, size_t len, GpoResult& result) {
  if (!response || len<4 || response[len-2]!=0x90 || response[len-1]!=0x00) return false;
  const size_t dataLen=len-2;
  if (response[0]==0x80) {
    size_t pos=1,l=0; if (!readLength(response,dataLen,pos,l) || l<2) return false;
    result.aip[0]=response[pos]; result.aip[1]=response[pos+1];
    size_t n=(l-2)<sizeof(result.afl)?(l-2):sizeof(result.afl); n-=n%4;
    memcpy(result.afl,response+pos+2,n); result.aflLen=(uint8_t)n;
  } else findGpoTags(response,dataLen,result);
  // A valid GPO does not necessarily contain an AFL. Contactless kernels may
  // return application data directly in the GPO response.
  return true;
}

inline String bcdDigits(const uint8_t* data, size_t len, bool stopAtD=false) {
  String s;
  for (size_t i=0;i<len;++i) {
    uint8_t n[2]={(uint8_t)(data[i]>>4),(uint8_t)(data[i]&0x0F)};
    for (uint8_t j=0;j<2;++j) { if (stopAtD && n[j]==0x0D) return s; if (n[j]<=9) s+=char('0'+n[j]); else if (n[j]==0x0F) return s; else return s; }
  }
  return s;
}

inline void appendTagName(String& out, uint32_t tag) {
  char buf[9];
  if (tag <= 0xFF) snprintf(buf, sizeof(buf), "%02lX", (unsigned long)tag);
  else if (tag <= 0xFFFF) snprintf(buf, sizeof(buf), "%04lX", (unsigned long)tag);
  else snprintf(buf, sizeof(buf), "%06lX", (unsigned long)tag);
  if (out.length()) out += " ";
  out += buf;
}

inline void collectTags(const uint8_t* data, size_t len, String& out, uint8_t depth = 0) {
  if (!data || !len || depth > 4) return;
  size_t pos = 0;
  while (pos < len) {
    uint32_t tag = 0; size_t l = 0;
    if (!readTag(data, len, pos, tag) || !readLength(data, len, pos, l)) return;
    appendTagName(out, tag);
    const uint8_t* v = data + pos;
    if ((tag & 0x20) != 0) collectTags(v, l, out, depth + 1);
    pos += l;
  }
}

inline void parseTrack2(const uint8_t* data, size_t len, CardData& card) {
  if (!data || !len) return;

  String digits;
  int separator = -1;
  for (size_t i = 0; i < len; ++i) {
    const uint8_t nibbles[2] = {(uint8_t)(data[i] >> 4), (uint8_t)(data[i] & 0x0F)};
    for (uint8_t j = 0; j < 2; ++j) {
      const uint8_t n = nibbles[j];
      if (n == 0x0D) {
        separator = (int)digits.length();
        continue;
      }
      if (n == 0x0F) break;
      if (n > 9) break;
      digits += char('0' + n);
    }
  }

  if (separator >= 0) {
    if (card.pan.length() == 0) card.pan = digits.substring(0, separator);
    // Track 2 layout after D: YYMM + service code + discretionary data.
    if (card.expiry.length() == 0 && digits.length() >= (size_t)separator + 4) {
      const String yymm = digits.substring(separator, separator + 4);
      card.expiry = yymm.substring(2, 4) + "/" + yymm.substring(0, 2);
    }
  }
}

inline void findCardData(const uint8_t* data, size_t len, CardData& card) {
  size_t pos=0;
  while (pos<len) {
    uint32_t tag=0; size_t l=0;
    if (!readTag(data,len,pos,tag) || !readLength(data,len,pos,l)) return;
    const uint8_t* v=data+pos;
    if (tag==0x5A && card.pan.length()==0) card.pan=bcdDigits(v,l);
    else if (tag==0x57 || tag==0x9F6B) parseTrack2(v,l,card);
    else if (tag==0x5F24 && l>=2 && card.expiry.length()==0) { String d=bcdDigits(v,2); if (d.length()>=4) card.expiry=d.substring(2,4)+"/"+d.substring(0,2); }
    else if ((tag&0x20)!=0) findCardData(v,l,card);
    pos+=l;
  }
}

inline size_t buildReadRecord(uint8_t record, uint8_t sfi, uint8_t out[5]) {
  if (!record || !sfi || sfi>30) return 0;
  out[0]=0x00; out[1]=0xB2; out[2]=record; out[3]=(uint8_t)((sfi<<3)|0x04); out[4]=0x00; return 5;
}

inline size_t buildSelectAid(const Application& app, uint8_t out[22]) {
  if (!app.aidLen || app.aidLen > sizeof(app.aid)) return 0;
  out[0] = 0x00; out[1] = 0xA4; out[2] = 0x04; out[3] = 0x00; out[4] = app.aidLen;
  memcpy(out + 5, app.aid, app.aidLen);
  out[5 + app.aidLen] = 0x00;
  return 6 + app.aidLen;
}

inline size_t buildSelectPpse(uint8_t out[20]) {
  static const uint8_t name[] = {'2','P','A','Y','.','S','Y','S','.','D','D','F','0','1'};
  out[0]=0x00; out[1]=0xA4; out[2]=0x04; out[3]=0x00; out[4]=sizeof(name);
  memcpy(out+5, name, sizeof(name)); out[19]=0x00;
  return 20;
}

} // namespace EmvReader
