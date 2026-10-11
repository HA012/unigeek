#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace unigeek_emv {
struct Tlv { uint32_t tag; const uint8_t* value; size_t length; bool constructed; };
inline bool nextTlv(const uint8_t* data,size_t len,size_t& pos,Tlv& out) {
  if (!data || pos>=len) return false;
  uint8_t first=data[pos++]; out.tag=first;
  if ((first&0x1f)==0x1f) {
    unsigned count=0; uint8_t b;
    do { if(pos>=len || ++count>3) return false; b=data[pos++]; out.tag=(out.tag<<8)|b; } while(b&0x80);
  }
  if(pos>=len) return false;
  uint8_t l=data[pos++]; size_t n=0;
  if((l&0x80)==0) n=l;
  else { unsigned bytes=l&0x7f; if(!bytes || bytes>sizeof(size_t) || bytes>3 || bytes>len-pos) return false;
    while(bytes--) { if(n>(SIZE_MAX>>8)) return false; n=(n<<8)|data[pos++]; } }
  if(n>len-pos) return false;
  out.value=data+pos; out.length=n; out.constructed=(first&0x20)!=0; pos+=n; return true;
}
inline bool validateTlvTree(const uint8_t* data, size_t len, unsigned depth=0) {
  if ((!data && len) || depth>8) return false;
  size_t pos=0;
  while (pos<len) {
    Tlv t;
    if (!nextTlv(data,len,pos,t)) return false;
    if (t.constructed && !validateTlvTree(t.value,t.length,depth+1)) return false;
  }
  return true;
}
inline bool findTag(const uint8_t* data,size_t len,uint32_t wanted,Tlv& result,unsigned depth=0) {
  if(depth>8 || (depth==0 && !validateTlvTree(data,len))) return false;
  size_t pos=0; Tlv t;
  while(pos<len) {
    if(!nextTlv(data,len,pos,t)) return false;
    if(t.tag==wanted) { result=t; return true; }
    if(t.constructed && findTag(t.value,t.length,wanted,result,depth+1)) return true;
  }
  return false;
}
struct AflEntry { uint8_t sfi, firstRecord, lastRecord, offlineRecords; };
inline bool parseAflEntry(const uint8_t* afl,size_t len,size_t offset,AflEntry& e) {
  if(!afl || len%4 || offset>len || len-offset<4) return false;
  const uint8_t b=afl[offset];
  if((b&7)!=0) return false;
  e.sfi=b>>3; e.firstRecord=afl[offset+1]; e.lastRecord=afl[offset+2]; e.offlineRecords=afl[offset+3];
  return e.sfi>=1 && e.sfi<=30 && e.firstRecord>=1 && e.firstRecord<=e.lastRecord && e.offlineRecords<=e.lastRecord-e.firstRecord+1;
}
inline bool extractAfl(const uint8_t* gpo,size_t len,const uint8_t*& afl,size_t& aflLen) {
  Tlv t;
  if(findTag(gpo,len,0x94,t)) { afl=t.value; aflLen=t.length; return aflLen>0 && aflLen%4==0; }
  if(findTag(gpo,len,0x80,t) && t.length>=2) { afl=t.value+2; aflLen=t.length-2; return aflLen>0 && aflLen%4==0; }
  return false;
}
inline bool buildReadRecord(uint8_t sfi,uint8_t record,uint8_t out[5]) {
  if(!out || sfi<1 || sfi>30 || !record) return false;
  out[0]=0x00; out[1]=0xB2; out[2]=record; out[3]=(uint8_t)((sfi<<3)|4); out[4]=0x00; return true;
}
inline bool buildSelect(const uint8_t* aid,size_t len,uint8_t* out,size_t cap,size_t& outLen) {
  outLen=0; if(!aid || !out || len<1 || len>16 || cap<len+6) return false;
  out[0]=0; out[1]=0xA4; out[2]=4; out[3]=0; out[4]=(uint8_t)len;
  memcpy(out+5,aid,len); out[len+5]=0; outLen=len+6; return true;
}

struct Aid { uint8_t bytes[16]; uint8_t length; };
inline bool collectAids(const uint8_t* data,size_t len,Aid* out,size_t capacity,size_t& count) {
  count=0; if(!data || (!out && capacity)) return false;
  struct Frame { const uint8_t* p; size_t len,pos; bool inApp; };
  Frame stack[10]; unsigned depth=1; stack[0]={data,len,0,false};
  while(depth) {
    Frame& f=stack[depth-1]; if(f.pos==f.len) { --depth; continue; }
    Tlv t; if(!nextTlv(f.p,f.len,f.pos,t)) return false;
    if(t.tag==0x4f && f.inApp) {
      if(t.length<5 || t.length>16) return false;
      bool duplicate=false;
      for(size_t i=0;i<count;i++) if(out[i].length==t.length && !memcmp(out[i].bytes,t.value,t.length)) duplicate=true;
      if(!duplicate) { if(count>=capacity) return false; memcpy(out[count].bytes,t.value,t.length); out[count].length=(uint8_t)t.length; ++count; }
    }
    if(t.constructed) { if(depth>=10) return false; stack[depth++]={t.value,t.length,0,f.inApp || t.tag==0x61}; }
  }
  return true;
}
struct DolItem { uint32_t tag; uint8_t length; };
inline bool nextDolItem(const uint8_t* dol,size_t len,size_t& pos,DolItem& item) {
  if(!dol || pos>=len) return false;
  uint8_t b=dol[pos++]; item.tag=b;
  if((b&0x1f)==0x1f) {
    unsigned n=0;
    do { if(pos>=len || ++n>3) return false; b=dol[pos++]; item.tag=(item.tag<<8)|b; } while(b&0x80);
  }
  if(pos>=len) return false;
  item.length=dol[pos++]; return true;
}
struct DolValue { uint32_t tag; const uint8_t* bytes; size_t length; };
inline bool buildGpo(const uint8_t* pdol,size_t pdolLen,const DolValue* values,size_t valueCount,
                     uint8_t* out,size_t capacity,size_t& outLen) {
  outLen=0; if(!out || (!pdol && pdolLen) || (!values && valueCount)) return false;
  uint8_t payload[255]; size_t used=0,pos=0;
  while(pos<pdolLen) {
    DolItem item; if(!nextDolItem(pdol,pdolLen,pos,item) || item.length>sizeof(payload)-used) return false;
    const DolValue* match=nullptr;
    for(size_t i=0;i<valueCount;i++) if(values[i].tag==item.tag) { match=&values[i]; break; }
    memset(payload+used,0,item.length);
    if(match) {
      if((!match->bytes && match->length) || match->length!=item.length) return false;
      if(item.length) memcpy(payload+used,match->bytes,item.length);
    }
    used+=item.length;
  }
  if(used>252 || capacity<used+8) return false;
  out[0]=0x80; out[1]=0xa8; out[2]=0; out[3]=0; out[4]=(uint8_t)(used+2);
  out[5]=0x83; out[6]=(uint8_t)used;
  if(used) memcpy(out+7,payload,used);
  out[7+used]=0; outLen=used+8; return true;
}
inline bool extractPdol(const uint8_t* selectData,size_t len,const uint8_t*& pdol,size_t& pdolLen) {
  pdol=nullptr; pdolLen=0; Tlv t;
  if(!findTag(selectData,len,0x9f38,t)) return false;
  pdol=t.value; pdolLen=t.length; return true;
}

struct CardInfo {
  char applicationLabel[33];
  char preferredName[33];
  uint8_t expiry[3];
  bool hasExpiry;
  bool hasApplicationLabel;
  bool hasPreferredName;
};
inline void initCardInfo(CardInfo& info) { memset(&info,0,sizeof(info)); }
inline bool copyEmvText(const Tlv& t,char* dest,size_t cap) {
  if(!dest || !cap || t.length>=cap) return false;
  for(size_t i=0;i<t.length;i++) {
    if(t.value[i]<0x20 || t.value[i]>0x7e) return false;
    dest[i]=(char)t.value[i];
  }
  dest[t.length]=0; return true;
}
inline bool scanCardInfo(const uint8_t* data,size_t len,CardInfo& info) {
  if(!data && len) return false;
  struct Frame { const uint8_t* p; size_t len,pos; };
  Frame stack[10]; unsigned depth=1; stack[0]={data,len,0};
  while(depth) {
    Frame& f=stack[depth-1];
    if(f.pos==f.len) { --depth; continue; }
    Tlv t; if(!nextTlv(f.p,f.len,f.pos,t)) return false;
    if(t.tag==0x50 && !info.hasApplicationLabel)
      info.hasApplicationLabel=copyEmvText(t,info.applicationLabel,sizeof(info.applicationLabel));
    if(t.tag==0x9f12 && !info.hasPreferredName)
      info.hasPreferredName=copyEmvText(t,info.preferredName,sizeof(info.preferredName));
    if(t.tag==0x5f24 && t.length==3 && !info.hasExpiry) {
      bool valid=true;
      for(unsigned i=0;i<3;i++) if((t.value[i]>>4)>9 || (t.value[i]&15)>9) valid=false;
      if(valid) { memcpy(info.expiry,t.value,3); info.hasExpiry=true; }
    }
    if(t.constructed) {
      if(depth>=10) return false;
      stack[depth++]={t.value,t.length,0};
    }
  }
  return true;
}
struct AflCursor { size_t offset; uint16_t nextRecord; };
inline void resetAflCursor(AflCursor& c) { c.offset=0; c.nextRecord=0; }
inline bool nextAflRecord(const uint8_t* afl,size_t len,AflCursor& c,uint8_t& sfi,uint8_t& record) {
  if(!afl || len%4) return false;
  while(c.offset<len) {
    AflEntry e;
    if(!parseAflEntry(afl,len,c.offset,e)) return false;
    if(!c.nextRecord) c.nextRecord=e.firstRecord;
    if(c.nextRecord<=e.lastRecord) {
      sfi=e.sfi; record=(uint8_t)c.nextRecord++;
      if(c.nextRecord>e.lastRecord) { c.offset+=4; c.nextRecord=0; }
      return true;
    }
    c.offset+=4; c.nextRecord=0;
  }
  return false;
}

inline bool apduSuccess(const uint8_t* response,size_t len) { return response && len>=2 && response[len-2]==0x90 && response[len-1]==0x00; }
inline size_t apduDataLength(size_t responseLen) { return responseLen>=2?responseLen-2:0; }

using ApduExchange = bool (*)(void* context, const uint8_t* command,
                              size_t commandLen, uint8_t* response,
                              size_t responseCapacity, size_t& responseLen);

enum class ReadStatus : uint8_t {
  Ok, InvalidArgument, TransportError, InvalidResponse,
  PpseFailed, NoApplication, SelectFailed, GpoFailed, InvalidAfl,
  RecordError, PartialRecords, NoRecords
};
struct ReadResult {
  ReadStatus status;
  CardInfo info;
  Aid selectedAid;
  bool hasSelectedAid;
  uint16_t recordsRead;
  uint16_t recordsFailed;
};
inline void initReadResult(ReadResult& r) {
  memset(&r, 0, sizeof(r));
  r.status = ReadStatus::InvalidArgument;
}
inline bool exchangeEmv(ApduExchange exchange, void* context,
                        const uint8_t* cmd, size_t cmdLen,
                        uint8_t* response, size_t capacity, size_t& responseLen) {
  responseLen = 0;
  if (!exchange || !cmd || !response || !capacity) return false;
  if (!exchange(context, cmd, cmdLen, response, capacity, responseLen)) return false;
  return responseLen >= 2 && responseLen <= capacity;
}

inline ReadStatus readEmv(ApduExchange exchange, void* context,
                          const DolValue* terminalValues, size_t terminalValueCount,
                          ReadResult& result) {
  initReadResult(result);
  if (!exchange || (!terminalValues && terminalValueCount)) return result.status;
  static uint8_t response[1024]; size_t responseLen = 0;
  static uint8_t command[264]; size_t commandLen = 0;
  static const uint8_t ppse[] = {
    '2','P','A','Y','.','S','Y','S','.','D','D','F','0','1'
  };
  if (!buildSelect(ppse, sizeof(ppse), command, sizeof(command), commandLen))
    return result.status;
  if (!exchangeEmv(exchange, context, command, commandLen,
                   response, sizeof(response), responseLen))
    return result.status = ReadStatus::TransportError;
  if (!apduSuccess(response, responseLen))
    return result.status = ReadStatus::PpseFailed;
  Aid aids[8]; size_t aidCount = 0;
  if (!collectAids(response, apduDataLength(responseLen), aids, 8, aidCount))
    return result.status = ReadStatus::InvalidResponse;
  if (!aidCount) return result.status = ReadStatus::NoApplication;

  ReadStatus lastFailure = ReadStatus::SelectFailed;
  for (size_t i = 0; i < aidCount; ++i) {
    CardInfo info; initCardInfo(info);
    if (!buildSelect(aids[i].bytes, aids[i].length,
                     command, sizeof(command), commandLen)) continue;
    if (!exchangeEmv(exchange, context, command, commandLen,
                     response, sizeof(response), responseLen)) {
      lastFailure = ReadStatus::TransportError; continue;
    }
    if (!apduSuccess(response, responseLen)) continue;
    if (!scanCardInfo(response, apduDataLength(responseLen), info)) {
      lastFailure = ReadStatus::InvalidResponse; continue;
    }
    const uint8_t* pdol = nullptr; size_t pdolLen = 0;
    extractPdol(response, apduDataLength(responseLen), pdol, pdolLen);
    if (!buildGpo(pdol, pdolLen, terminalValues, terminalValueCount,
                  command, sizeof(command), commandLen)) {
      lastFailure = ReadStatus::GpoFailed; continue;
    }
    if (!exchangeEmv(exchange, context, command, commandLen,
                     response, sizeof(response), responseLen)) {
      lastFailure = ReadStatus::TransportError; continue;
    }
    if (!apduSuccess(response, responseLen)) {
      lastFailure = ReadStatus::GpoFailed; continue;
    }
    const size_t dataLen = apduDataLength(responseLen);
    if (!scanCardInfo(response, dataLen, info)) {
      lastFailure = ReadStatus::InvalidResponse; continue;
    }
    const uint8_t* aflPtr = nullptr; size_t aflLen = 0;
    if (!extractAfl(response, dataLen, aflPtr, aflLen) || aflLen > 252) {
      lastFailure = ReadStatus::InvalidAfl; continue;
    }
    static uint8_t afl[252]; memcpy(afl, aflPtr, aflLen);
    bool validAfl = true;
    for (size_t j = 0; j < aflLen; j += 4) {
      AflEntry e;
      if (!parseAflEntry(afl, aflLen, j, e)) { validAfl = false; break; }
    }
    if (!validAfl) { lastFailure = ReadStatus::InvalidAfl; continue; }
    AflCursor cursor; resetAflCursor(cursor);
    uint8_t sfi, record;
    uint16_t readCount = 0, failedCount = 0;
    while (nextAflRecord(afl, aflLen, cursor, sfi, record)) {
      if (!buildReadRecord(sfi, record, command)) {
        ++failedCount; continue;
      }
      if (!exchangeEmv(exchange, context, command, 5,
                       response, sizeof(response), responseLen) ||
          !apduSuccess(response, responseLen)) {
        ++failedCount; continue;
      }
      if (!scanCardInfo(response, apduDataLength(responseLen), info)) {
        ++failedCount; continue;
      }
      ++readCount;
    }
    result.info = info;
    result.selectedAid = aids[i];
    result.hasSelectedAid = true;
    result.recordsRead = readCount;
    result.recordsFailed = failedCount;
    return result.status = failedCount ? (readCount ? ReadStatus::PartialRecords : ReadStatus::NoRecords) : ReadStatus::Ok;
  }
  return result.status = lastFailure;
}

}
