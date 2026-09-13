#include "ST25R3916Experimental.h"

#if defined(DEVICE_HAS_ST25R3916)

#include <mbedtls/aes.h>
#include <esp_random.h>

namespace ST25R3916Experimental {
namespace {

bool responseOk(const uint8_t* rx, size_t len) {
  return rx && len >= 1 && (rx[0] & 0x01U) == 0;
}

void rotateLeft16(const uint8_t in[16], uint8_t out[16]) {
  memcpy(out, in + 1, 15);
  out[15] = in[0];
}

bool aesCbc(const uint8_t key[16], bool encrypt, const uint8_t iv[16],
            const uint8_t* in, uint8_t* out, size_t len) {
  if (!key || !iv || !in || !out || !len || (len & 15U)) return false;
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if ((encrypt ? mbedtls_aes_setkey_enc(&aes, key, 128) : mbedtls_aes_setkey_dec(&aes, key, 128)) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }
  uint8_t workIv[16];
  memcpy(workIv, iv, sizeof(workIv));
  const int rc = mbedtls_aes_crypt_cbc(&aes, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                       len, workIv, in, out);
  mbedtls_aes_free(&aes);
  return rc == 0;
}

bool type4SelectNdef(ST25R3916Backend& dev, uint16_t& fileId, size_t& capacity, bool& writable) {
  static const uint8_t selApp[] = {0x00,0xA4,0x04,0x00,0x07,0xD2,0x76,0x00,0x00,0x85,0x01,0x01,0x00};
  uint8_t rx[260] = {};
  size_t n = 0;
  if (!dev.isoDepTransceive(selApp, sizeof(selApp), rx, sizeof(rx), n) ||
      n < 2 || rx[n - 2] != 0x90 || rx[n - 1] != 0x00) return false;

  static const uint8_t selCc[] = {0x00,0xA4,0x00,0x0C,0x02,0xE1,0x03};
  if (!dev.isoDepTransceive(selCc, sizeof(selCc), rx, sizeof(rx), n) ||
      n < 2 || rx[n - 2] != 0x90 || rx[n - 1] != 0x00) return false;

  // Read CCLEN first. Type 4 tags may expose more than one File Control TLV,
  // so do not assume that the first NDEF File Control TLV is always at 0x07.
  static const uint8_t readCcLen[] = {0x00,0xB0,0x00,0x00,0x02};
  if (!dev.isoDepTransceive(readCcLen, sizeof(readCcLen), rx, sizeof(rx), n) ||
      n < 4 || rx[n - 2] != 0x90 || rx[n - 1] != 0x00) return false;
  const size_t ccLen = ((size_t)rx[0] << 8) | rx[1];
  if (ccLen < 15 || ccLen > 0xF0) return false;

  uint8_t readCc[] = {0x00,0xB0,0x00,0x00,(uint8_t)ccLen};
  if (!dev.isoDepTransceive(readCc, sizeof(readCc), rx, sizeof(rx), n) ||
      n != ccLen + 2 || rx[n - 2] != 0x90 || rx[n - 1] != 0x00) return false;

  bool found = false;
  for (size_t p = 7; p + 1 < ccLen;) {
    const uint8_t type = rx[p++];
    const size_t len = rx[p++];
    if (p + len > ccLen) return false;
    if (type == 0x04 && len >= 6) {
      fileId = (uint16_t)((rx[p] << 8) | rx[p + 1]);
      const size_t fileSize = ((size_t)rx[p + 2] << 8) | rx[p + 3];
      if (fileSize < 2) return false;
      // FLEN includes the 2-byte NLEN field at the beginning of the NDEF file.
      capacity = fileSize - 2U;
      writable = rx[p + 5] == 0x00;
      found = true;
      break;
    }
    p += len;
  }
  if (!found) return false;

  uint8_t selFile[] = {0x00,0xA4,0x00,0x0C,0x02,(uint8_t)(fileId>>8),(uint8_t)fileId};
  return dev.isoDepTransceive(selFile, sizeof(selFile), rx, sizeof(rx), n) &&
         n >= 2 && rx[n - 2] == 0x90 && rx[n - 1] == 0x00;
}

bool typeVCommand(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                  uint8_t cmd, const uint8_t* params, size_t paramsLen,
                  uint8_t* rx, size_t rxMax, size_t& rxLen) {
  if (tag.technology != ST25R3916Backend::Technology::NFC_V || tag.nfcidLen < 8) return false;
  uint8_t tx[64] = {};
  if (paramsLen + 10 > sizeof(tx)) return false;
  tx[0] = 0x22; // high data rate + addressed
  tx[1] = cmd;
  memcpy(tx + 2, tag.nfcid, 8);
  if (paramsLen) memcpy(tx + 10, params, paramsLen);
  return dev.activeRfTransceive(tx, paramsLen + 10, rx, rxMax, rxLen, 120) && responseOk(rx, rxLen);
}

bool felicaExchange(ST25R3916Backend& dev, const uint8_t* body, size_t bodyLen,
                    uint8_t* rx, size_t rxMax, size_t& rxLen) {
  if (!body || bodyLen + 1 > 255) return false;
  uint8_t tx[255] = {};
  tx[0] = (uint8_t)(bodyLen + 1);
  memcpy(tx + 1, body, bodyLen);
  return dev.activeRfTransceive(tx, bodyLen + 1, rx, rxMax, rxLen, 80) && rxLen >= 2 && rx[0] == rxLen;
}

uint16_t attrChecksum(const uint8_t attr[16]) {
  uint16_t s = 0;
  for (uint8_t i = 0; i < 14; ++i) s = (uint16_t)(s + attr[i]);
  return s;
}

bool typeVLayout(const uint8_t* mem, size_t total, size_t& cc, size_t& capacity) {
  cc = 0;
  capacity = 0;
  if (!mem || total < 8) return false;
  size_t advertised = 0;
  if ((mem[0] == 0xE1 || mem[0] == 0xE2) && mem[2] != 0) {
    cc = 4;
    advertised = (size_t)mem[2] * 8U;
  } else if (mem[0] == 0xE2 && total >= 8) {
    cc = 8;
    advertised = (size_t)(((uint16_t)mem[6] << 8) | mem[7]) * 8U;
  } else {
    return false;
  }
  if (!advertised || cc >= total) return false;
  capacity = min(advertised, total - cc);
  return capacity > 0;
}

bool appendTypeVTlv(uint8_t* area, size_t areaCap, size_t& pos,
                    uint8_t type, const uint8_t* value, size_t len) {
  const size_t header = len < 0xFFU ? 2U : 4U;
  if (!area || pos + header + len > areaCap) return false;
  area[pos++] = type;
  if (len < 0xFFU) {
    area[pos++] = (uint8_t)len;
  } else {
    if (len > 0xFFFFU) return false;
    area[pos++] = 0xFF;
    area[pos++] = (uint8_t)(len >> 8);
    area[pos++] = (uint8_t)len;
  }
  if (len && value) memcpy(area + pos, value, len);
  pos += len;
  return true;
}

bool rewriteTypeVArea(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                      const TypeVInfo& info, uint8_t* oldMem, const uint8_t* newMem,
                      size_t bytesToConsider) {
  if (!oldMem || !newMem || !info.blockSize) return false;
  const uint16_t blocks = (uint16_t)((bytesToConsider + info.blockSize - 1U) / info.blockSize);
  for (uint16_t b = 0; b < blocks; ++b) {
    const size_t off = (size_t)b * info.blockSize;
    if (memcmp(oldMem + off, newMem + off, info.blockSize) == 0) continue;
    if (!typeVWriteBlock(dev, tag, b, newMem + off, info.blockSize)) return false;
  }
  return true;
}

bool rebuildTypeVArea(const uint8_t* oldArea, size_t areaCap,
                      const uint8_t* ndef, size_t ndefLen,
                      bool preserveNonControlTlvs, uint8_t* newArea) {
  if (!oldArea || !newArea) return false;
  memset(newArea, 0, areaCap);
  size_t out = 0;
  size_t p = 0;
  bool insertedNdef = false;
  bool sawTerminator = false;
  while (p < areaCap) {
    const size_t tlvStart = p;
    const uint8_t type = oldArea[p++];
    if (type == 0x00) continue;
    if (type == 0xFE) { sawTerminator = true; break; }
    if (p >= areaCap) return false;
    size_t len = oldArea[p++];
    if (len == 0xFF) {
      if (p + 1 >= areaCap) return false;
      len = ((size_t)oldArea[p] << 8) | oldArea[p + 1];
      p += 2;
    }
    if (p + len > areaCap) return false;
    const uint8_t* value = oldArea + p;
    if (type == 0x03) {
      if (!insertedNdef) {
        if (!appendTypeVTlv(newArea, areaCap, out, 0x03, ndef, ndefLen)) return false;
        insertedNdef = true;
      }
    } else if (preserveNonControlTlvs || type == 0x01 || type == 0x02) {
      const size_t rawLen = (p + len) - tlvStart;
      if (out + rawLen > areaCap) return false;
      memcpy(newArea + out, oldArea + tlvStart, rawLen);
      out += rawLen;
    }
    p += len;
  }
  if (!insertedNdef) {
    if (!appendTypeVTlv(newArea, areaCap, out, 0x03, ndef, ndefLen)) return false;
  }
  if (out >= areaCap) return false;
  newArea[out++] = 0xFE;
  (void)sawTerminator;
  return true;
}

} // namespace

bool desfireExchange(ST25R3916Backend& dev, uint8_t ins, const uint8_t* data, size_t dataLen,
                     uint8_t* out, size_t outMax, size_t& outLen, uint8_t& status) {
  outLen = 0; status = 0xFF;
  if (dataLen > 220) return false;
  uint8_t apdu[256] = {0x90, ins, 0x00, 0x00, (uint8_t)dataLen};
  if (dataLen) memcpy(apdu + 5, data, dataLen);
  size_t apduLen = 5 + dataLen;
  apdu[apduLen++] = 0x00;

  for (uint8_t frame = 0; frame < 16; ++frame) {
    uint8_t rx[260] = {}; size_t n = 0;
    if (!dev.isoDepTransceive(apdu, apduLen, rx, sizeof(rx), n) || n < 2 || rx[n-2] != 0x91) return false;
    const size_t payload = n - 2;
    if (out && payload) {
      if (outLen + payload > outMax) return false;
      memcpy(out + outLen, rx, payload); outLen += payload;
    }
    status = rx[n-1];
    if (status != 0xAF) return status == 0x00;
    apdu[0]=0x90; apdu[1]=0xAF; apdu[2]=0; apdu[3]=0; apdu[4]=0; apdu[5]=0; apduLen=6;
  }
  return false;
}

bool desfireProbe(ST25R3916Backend& dev) {
  uint8_t version[96] = {};
  size_t n = 0;
  uint8_t status = 0xFF;
  // GetVersion is a DESFire-native command. A generic ISO-DEP Type 4A card
  // should not be classified as DESFire unless this command completes with a
  // valid DESFire status and at least one version frame worth of payload.
  return desfireExchange(dev, 0x60, nullptr, 0, version, sizeof(version), n, status) &&
         status == 0x00 && n >= 7;
}

bool desfireAuthenticateAes(ST25R3916Backend& dev, uint8_t keyNo, const uint8_t key[16]) {
  uint8_t apdu[] = {0x90,0xAA,0x00,0x00,0x01,keyNo,0x00};
  uint8_t rx[80] = {}; size_t n = 0;
  if (!dev.isoDepTransceive(apdu,sizeof(apdu),rx,sizeof(rx),n) || n != 18 || rx[16] != 0x91 || rx[17] != 0xAF) return false;
  uint8_t iv0[16] = {}, rndB[16] = {}, rndBp[16] = {};
  if (!aesCbc(key,false,iv0,rx,rndB,16)) return false;
  rotateLeft16(rndB,rndBp);
  uint8_t rndA[16] = {};
  for (uint8_t i=0;i<16;i+=4) { uint32_t r=esp_random(); memcpy(rndA+i,&r,4); }
  uint8_t plain[32] = {}; memcpy(plain,rndA,16); memcpy(plain+16,rndBp,16);
  uint8_t enc[32] = {}, iv1[16] = {}; memcpy(iv1,rx,16);
  if (!aesCbc(key,true,iv1,plain,enc,32)) return false;
  uint8_t af[39] = {0x90,0xAF,0x00,0x00,0x20}; memcpy(af+5,enc,32); af[37]=0x00;
  if (!dev.isoDepTransceive(af,38,rx,sizeof(rx),n) || n != 18 || rx[16] != 0x91 || rx[17] != 0x00) return false;
  uint8_t iv2[16] = {}; memcpy(iv2,enc+16,16); uint8_t rndAprime[16] = {};
  if (!aesCbc(key,false,iv2,rx,rndAprime,16)) return false;
  uint8_t expected[16] = {}; rotateLeft16(rndA,expected);
  return memcmp(rndAprime,expected,16) == 0;
}

bool type4ReadNdef(ST25R3916Backend& dev, uint8_t* out, size_t outMax, size_t& outLen, size_t& capacity) {
  outLen=0; capacity=0; uint16_t fid=0; bool writable=false;
  if (!type4SelectNdef(dev,fid,capacity,writable)) return false;
  uint8_t rx[260] = {}; size_t n=0; static const uint8_t readLen[]={0x00,0xB0,0x00,0x00,0x02};
  if (!dev.isoDepTransceive(readLen,sizeof(readLen),rx,sizeof(rx),n) || n<4 || rx[n-2]!=0x90 || rx[n-1]!=0) return false;
  size_t want=((size_t)rx[0]<<8)|rx[1]; if(want>outMax || want>capacity) return false;
  size_t off=0;
  while(off<want){ size_t chunk=min((size_t)0xF0,want-off); uint16_t pos=(uint16_t)(off+2); uint8_t cmd[]={0x00,0xB0,(uint8_t)(pos>>8),(uint8_t)pos,(uint8_t)chunk};
    if(!dev.isoDepTransceive(cmd,sizeof(cmd),rx,sizeof(rx),n)||n<2||rx[n-2]!=0x90||rx[n-1]!=0||n-2!=chunk)return false; memcpy(out+off,rx,chunk);off+=chunk; }
  outLen=want; return true;
}

bool type4WriteNdef(ST25R3916Backend& dev, const uint8_t* ndef, size_t ndefLen, size_t& capacity) {
  capacity=0; uint16_t fid=0; bool writable=false; if(!type4SelectNdef(dev,fid,capacity,writable)||!writable||ndefLen>capacity||ndefLen>0xFFFFU)return false;
  uint8_t rx[32]={};size_t n=0;uint8_t zero[]={0x00,0xD6,0x00,0x00,0x02,0x00,0x00}; if(!dev.isoDepTransceive(zero,sizeof(zero),rx,sizeof(rx),n)||n<2||rx[n-2]!=0x90||rx[n-1]!=0)return false;
  size_t off=0; while(off<ndefLen){size_t chunk=min((size_t)0xD0,ndefLen-off);uint16_t pos=(uint16_t)(off+2);uint8_t cmd[5+0xD0]={0x00,0xD6,(uint8_t)(pos>>8),(uint8_t)pos,(uint8_t)chunk};memcpy(cmd+5,ndef+off,chunk);if(!dev.isoDepTransceive(cmd,5+chunk,rx,sizeof(rx),n)||n<2||rx[n-2]!=0x90||rx[n-1]!=0)return false;off+=chunk;}
  uint8_t lenCmd[]={0x00,0xD6,0x00,0x00,0x02,(uint8_t)(ndefLen>>8),(uint8_t)ndefLen};return dev.isoDepTransceive(lenCmd,sizeof(lenCmd),rx,sizeof(rx),n)&&n>=2&&rx[n-2]==0x90&&rx[n-1]==0;
}

bool typeVGetSystemInfo(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag, TypeVInfo& info) {
  info = TypeVInfo{};
  uint8_t rx[64] = {};
  size_t n = 0;

  // First try the ISO15693 Get System Information command. Most tags expose
  // DSFID/AFI and memory geometry here. Large ST25V devices may deliberately
  // omit the memory-size field because the legacy field only represents up to
  // 256 blocks.
  if (typeVCommand(dev, tag, 0x2B, nullptr, 0, rx, sizeof(rx), n) && n >= 10) {
    size_t p = 1;
    const uint8_t flags = rx[p++];
    p += 8;  // UID, in RF transmission order
    if ((flags & 0x01U) && p < n) { info.dsfid = rx[p++]; info.hasDsfid = true; }
    if ((flags & 0x02U) && p < n) { info.afi = rx[p++]; info.hasAfi = true; }
    if ((flags & 0x04U) && p + 1 < n) {
      info.blocks = (uint16_t)rx[p++] + 1U;
      info.blockSize = (uint8_t)((rx[p++] & 0x1FU) + 1U);
    }
    if (info.blocks && info.blockSize) return true;
  }

  // ISO15693 Extended Get System Information (0x3B) puts the information
  // request byte before the optional UID. Request DSFID, AFI, memory size and
  // IC reference; bit 4 (MOI) is required by ST25TV/ST25DV implementations.
  // This is the path needed for ST25TV16K/64K, where the standard 0x2B reply
  // omits memory size.
  if (tag.technology != ST25R3916Backend::Technology::NFC_V || tag.nfcidLen < 8) return false;
  uint8_t tx[11] = {0x22, 0x3B, 0x1F};
  memcpy(tx + 3, tag.nfcid, 8);
  n = 0;
  if (!dev.activeRfTransceive(tx, sizeof(tx), rx, sizeof(rx), n, 150) || !responseOk(rx, n) || n < 10) return false;

  size_t p = 1;
  const uint8_t flags = rx[p++];
  p += 8;
  if ((flags & 0x01U) && p < n) { info.dsfid = rx[p++]; info.hasDsfid = true; }
  if ((flags & 0x02U) && p < n) { info.afi = rx[p++]; info.hasAfi = true; }
  if ((flags & 0x04U) && p + 2 < n) {
    // Extended memory geometry is encoded as a 16-bit (number of blocks - 1)
    // followed by (block size - 1). Bytes are sent least-significant first.
    const uint16_t lastBlock = (uint16_t)rx[p] | ((uint16_t)rx[p + 1] << 8);
    info.blocks = lastBlock + 1U;
    info.blockSize = (uint8_t)((rx[p + 2] & 0x1FU) + 1U);
  }
  return info.blocks && info.blockSize;
}

bool typeVReadBlock(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint16_t block,uint8_t*out,size_t outMax,size_t&outLen){uint8_t par[2];size_t pn;if(block<=0xFF){par[0]=(uint8_t)block;pn=1;}else{par[0]=(uint8_t)block;par[1]=(uint8_t)(block>>8);pn=2;}uint8_t rx[80]={};size_t n=0;if(!typeVCommand(dev,tag,block<=0xFF?0x20:0x30,par,pn,rx,sizeof(rx),n)||n<2)return false;outLen=n-1;if(outLen>outMax)return false;memcpy(out,rx+1,outLen);return true;}
bool typeVWriteBlock(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint16_t block,const uint8_t*data,size_t dataLen){if(!data||!dataLen||dataLen>32)return false;uint8_t par[34]={};size_t p=0;par[p++]=(uint8_t)block;if(block>0xFF)par[p++]=(uint8_t)(block>>8);memcpy(par+p,data,dataLen);p+=dataLen;uint8_t rx[16]={};size_t n=0;return typeVCommand(dev,tag,block<=0xFF?0x21:0x31,par,p,rx,sizeof(rx),n);}
bool typeVLockBlock(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint16_t block){uint8_t par[2]={(uint8_t)block,(uint8_t)(block>>8)};uint8_t rx[16]={};size_t n=0;return typeVCommand(dev,tag,block<=0xFF?0x22:0x32,par,block<=0xFF?1:2,rx,sizeof(rx),n);}
bool typeVSecurityStatus(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                         uint16_t block, uint8_t& status) {
  uint8_t rx[16] = {};
  size_t n = 0;
  if (block <= 0xFF) {
    const uint8_t par[2] = {(uint8_t)block, 0x00};  // one block
    if (!typeVCommand(dev, tag, 0x2C, par, sizeof(par), rx, sizeof(rx), n) || n < 2) return false;
  } else {
    if (tag.technology != ST25R3916Backend::Technology::NFC_V || tag.nfcidLen < 8) return false;
    uint8_t tx[14] = {0x22, 0x3C};  // Extended Get Multiple Block Security Status
    memcpy(tx + 2, tag.nfcid, 8);
    tx[10] = (uint8_t)block;
    tx[11] = (uint8_t)(block >> 8);
    tx[12] = 0x00;  // number of blocks - 1, LSB
    tx[13] = 0x00;  // number of blocks - 1, MSB
    if (!dev.activeRfTransceive(tx, sizeof(tx), rx, sizeof(rx), n, 120) || !responseOk(rx, n) || n < 2) return false;
  }
  status = rx[1];
  return true;
}

bool typeVReadNdef(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                   uint8_t* out, size_t outMax, size_t& outLen, size_t& capacity) {
  outLen = 0;
  capacity = 0;
  TypeVInfo info;
  if (!typeVGetSystemInfo(dev, tag, info)) return false;
  const size_t total = (size_t)info.blocks * info.blockSize;
  if (total < 8 || total > 8192) return false;

  uint8_t* mem = new uint8_t[total];
  if (!mem) return false;
  bool ok = true;
  for (uint16_t b = 0; b < info.blocks && ok; ++b) {
    size_t n = 0;
    ok = typeVReadBlock(dev, tag, b, mem + (size_t)b * info.blockSize,
                        total - (size_t)b * info.blockSize, n) && n == info.blockSize;
  }
  if (!ok) { delete[] mem; return false; }

  // NFC Forum Type 5: E1 normally means a 4-byte CC. E2 and MLEN==0
  // denote the 8-byte extended CC used by large-memory tags. Some tags may
  // legally use E2 with a 4-byte CC, so MLEN is the authoritative indicator.
  size_t cc = 0;
  size_t advertised = 0;
  if (mem[0] == 0xE1 && mem[2] != 0) {
    cc = 4;
    advertised = (size_t)mem[2] * 8U;
  } else if (mem[0] == 0xE2 && mem[2] != 0) {
    cc = 4;
    advertised = (size_t)mem[2] * 8U;
  } else if (mem[0] == 0xE2 && total >= 8) {
    cc = 8;
    advertised = (size_t)(((uint16_t)mem[6] << 8) | mem[7]) * 8U;
  } else {
    delete[] mem;
    return false;
  }
  if (!advertised || cc >= total) { delete[] mem; return false; }
  capacity = min(advertised, total - cc);
  const size_t areaEnd = cc + capacity;

  size_t p = cc;
  while (p < areaEnd) {
    const uint8_t type = mem[p++];
    if (type == 0x00) continue;  // NULL TLV
    if (type == 0xFE || p >= areaEnd) break;
    size_t len = mem[p++];
    if (len == 0xFF) {
      if (p + 1 >= areaEnd) break;
      len = ((size_t)mem[p] << 8) | mem[p + 1];
      p += 2;
    }
    if (p + len > areaEnd) break;
    if (type == 0x03) {
      if (len > outMax) { delete[] mem; return false; }
      if (len) memcpy(out, mem + p, len);
      outLen = len;
      delete[] mem;
      return true;
    }
    p += len;
  }
  delete[] mem;
  return false;
}

bool typeVWriteNdef(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                    const uint8_t* ndef, size_t ndefLen, size_t& capacity, bool allowFormat) {
  capacity = 0;
  TypeVInfo info;
  if (!typeVGetSystemInfo(dev, tag, info) || info.blockSize == 0) return false;
  const size_t total = (size_t)info.blocks * info.blockSize;
  if (total < 8 || total > 8192) return false;

  uint8_t* oldMem = new uint8_t[total];
  uint8_t* newMem = new uint8_t[total];
  if (!oldMem || !newMem) { delete[] oldMem; delete[] newMem; return false; }
  bool ok = true;
  for (uint16_t b = 0; b < info.blocks && ok; ++b) {
    size_t n = 0;
    ok = typeVReadBlock(dev, tag, b, oldMem + (size_t)b * info.blockSize,
                        total - (size_t)b * info.blockSize, n) && n == info.blockSize;
  }
  if (!ok) { delete[] oldMem; delete[] newMem; return false; }
  memcpy(newMem, oldMem, total);

  size_t cc = 0;
  if (!typeVLayout(oldMem, total, cc, capacity)) {
    if (!allowFormat) { delete[] oldMem; delete[] newMem; return false; }

    // Formatting is the only path that may create a Capability Container from
    // scratch. Normal Write/Erase operations must preserve the tag layout.
    size_t advertised = 0;
    if (total <= 2040) {
      cc = 4;
      advertised = ((total - cc) / 8U) * 8U;
      if (!advertised || advertised / 8U > 0xFFU) { delete[] oldMem; delete[] newMem; return false; }
      newMem[0] = 0xE1;
      newMem[1] = 0x40;
      newMem[2] = (uint8_t)(advertised / 8U);
      newMem[3] = 0x00;
    } else {
      cc = 8;
      advertised = ((total - cc) / 8U) * 8U;
      const size_t mlen = advertised / 8U;
      if (!advertised || mlen > 0xFFFFU) { delete[] oldMem; delete[] newMem; return false; }
      newMem[0] = 0xE2;
      newMem[1] = 0x40;
      newMem[2] = 0x00;
      newMem[3] = 0x00;
      newMem[4] = 0x00;
      newMem[5] = 0x00;
      newMem[6] = (uint8_t)(mlen >> 8);
      newMem[7] = (uint8_t)mlen;
    }
    capacity = min(advertised, total - cc);
    memset(newMem + cc, 0, capacity);
    size_t p = 0;
    if (!appendTypeVTlv(newMem + cc, capacity, p, 0x03, ndef, ndefLen) || p >= capacity) {
      delete[] oldMem; delete[] newMem; return false;
    }
    newMem[cc + p] = 0xFE;
  } else {
    // Existing Type 5 layout: replace only the NDEF TLV and preserve Lock
    // Control, Memory Control, proprietary and any other valid TLVs.
    uint8_t* area = new uint8_t[capacity];
    if (!area) { delete[] oldMem; delete[] newMem; return false; }
    ok = rebuildTypeVArea(oldMem + cc, capacity, ndef, ndefLen, true, area);
    if (ok) memcpy(newMem + cc, area, capacity);
    delete[] area;
    if (!ok) { delete[] oldMem; delete[] newMem; return false; }
  }

  const size_t bytesToWrite = cc + capacity;
  ok = rewriteTypeVArea(dev, tag, info, oldMem, newMem, bytesToWrite);
  delete[] oldMem;
  delete[] newMem;
  return ok;
}

bool typeVEraseTagSafe(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                       size_t& capacity) {
  capacity = 0;
  TypeVInfo info;
  if (!typeVGetSystemInfo(dev, tag, info) || info.blockSize == 0) return false;
  const size_t total = (size_t)info.blocks * info.blockSize;
  if (total < 8 || total > 8192) return false;

  uint8_t* oldMem = new uint8_t[total];
  uint8_t* newMem = new uint8_t[total];
  if (!oldMem || !newMem) { delete[] oldMem; delete[] newMem; return false; }
  bool ok = true;
  for (uint16_t b = 0; b < info.blocks && ok; ++b) {
    size_t n = 0;
    ok = typeVReadBlock(dev, tag, b, oldMem + (size_t)b * info.blockSize,
                        total - (size_t)b * info.blockSize, n) && n == info.blockSize;
  }
  if (!ok) { delete[] oldMem; delete[] newMem; return false; }
  memcpy(newMem, oldMem, total);

  size_t cc = 0;
  if (!typeVLayout(oldMem, total, cc, capacity)) {
    delete[] oldMem; delete[] newMem; return false;
  }

  // Safe Erase Tag intentionally preserves the Capability Container and the
  // structural Lock/Memory Control TLVs. NDEF and proprietary user TLVs are
  // removed, leaving a valid empty NDEF TLV and terminator.
  uint8_t* area = new uint8_t[capacity];
  if (!area) { delete[] oldMem; delete[] newMem; return false; }
  const uint8_t empty = 0;
  ok = rebuildTypeVArea(oldMem + cc, capacity, &empty, 0, false, area);
  if (ok) memcpy(newMem + cc, area, capacity);
  delete[] area;
  if (ok) ok = rewriteTypeVArea(dev, tag, info, oldMem, newMem, cc + capacity);

  delete[] oldMem;
  delete[] newMem;
  return ok;
}

bool typeVPresentIcodePassword(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint8_t passwordId,const uint8_t password[4]){if(tag.nfcidLen<8||tag.nfcid[6]!=0x04)return false;uint8_t rx[32]={};size_t n=0;uint8_t getRnd[]={0x22,0xB2,0x04};uint8_t tx[32]={};memcpy(tx,getRnd,3);memcpy(tx+3,tag.nfcid,8);if(!dev.activeRfTransceive(tx,11,rx,sizeof(rx),n,120)||n<3||!responseOk(rx,n))return false;uint8_t r0=rx[1],r1=rx[2];size_t p=0;tx[p++]=0x22;tx[p++]=0xB3;tx[p++]=0x04;memcpy(tx+p,tag.nfcid,8);p+=8;tx[p++]=passwordId;for(uint8_t i=0;i<4;++i)tx[p++]=password[i]^((i&1)?r1:r0);return dev.activeRfTransceive(tx,p,rx,sizeof(rx),n,120)&&responseOk(rx,n);}

bool typeVPresentStPassword(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                            uint8_t passwordId, const uint8_t* password, size_t passwordLen) {
  // STMicroelectronics ISO15693 UID manufacturer byte is 0x02. RFAL stores
  // NFC-V UIDs in RF transmission order, hence the manufacturer byte is at 6.
  if (tag.nfcidLen < 8 || tag.nfcid[6] != 0x02 || !password ||
      (passwordLen != 4 && passwordLen != 8)) return false;

  uint8_t tx[32] = {0x22, 0xB4, 0x02};  // Get Random Number
  memcpy(tx + 3, tag.nfcid, 8);
  uint8_t rx[32] = {};
  size_t n = 0;
  if (!dev.activeRfTransceive(tx, 11, rx, sizeof(rx), n, 150) ||
      !responseOk(rx, n) || n < 3) return false;

  const uint8_t rnd0 = rx[1];
  const uint8_t rnd1 = rx[2];
  size_t p = 0;
  tx[p++] = 0x22;
  tx[p++] = 0xB3;
  tx[p++] = 0x02;
  memcpy(tx + p, tag.nfcid, 8);
  p += 8;
  tx[p++] = passwordId;
  for (size_t i = 0; i < passwordLen; ++i) {
    tx[p++] = password[i] ^ ((i & 1U) ? rnd1 : rnd0);
  }
  return dev.activeRfTransceive(tx, p, rx, sizeof(rx), n, 150) && responseOk(rx, n);
}

bool felicaRequestSystemCodes(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint16_t*systems,size_t maxSystems,size_t&count){count=0;if(tag.nfcidLen<8)return false;uint8_t body[9]={0x0C};memcpy(body+1,tag.nfcid,8);uint8_t rx[96]={};size_t n=0;if(!felicaExchange(dev,body,sizeof(body),rx,sizeof(rx),n)||n<11||rx[1]!=0x0D)return false;size_t c=rx[10];if(11+c*2>n)return false;count=min(c,maxSystems);for(size_t i=0;i<count;++i)systems[i]=(uint16_t)((rx[11+i*2]<<8)|rx[12+i*2]);return true;}
bool felicaSearchService(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint16_t index,uint16_t&serviceCode){if(tag.nfcidLen<8)return false;uint8_t body[11]={0x0A};memcpy(body+1,tag.nfcid,8);body[9]=(uint8_t)index;body[10]=(uint8_t)(index>>8);uint8_t rx[32]={};size_t n=0;if(!felicaExchange(dev,body,sizeof(body),rx,sizeof(rx),n)||n<12||rx[1]!=0x0B)return false;serviceCode=(uint16_t)(rx[10]|(rx[11]<<8));return serviceCode!=0xFFFF;}
bool felicaRequestService(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                          uint16_t serviceCode, uint16_t& keyVersion) {
  if (tag.nfcidLen < 8) return false;
  uint8_t body[12] = {0x02};
  memcpy(body + 1, tag.nfcid, 8);
  body[9] = 1;  // one node
  body[10] = (uint8_t)serviceCode;
  body[11] = (uint8_t)(serviceCode >> 8);
  uint8_t rx[32] = {};
  size_t n = 0;
  if (!felicaExchange(dev, body, sizeof(body), rx, sizeof(rx), n) ||
      n < 13 || rx[1] != 0x03 || rx[10] != 1) return false;
  // Response: LEN, 0x03, IDm[8], Number of Node, Node Key Version[2].
  keyVersion = (uint16_t)(rx[11] | ((uint16_t)rx[12] << 8));
  return keyVersion != 0xFFFF;
}
bool felicaReadBlock(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint16_t serviceCode,uint16_t block,uint8_t data[16]){if(tag.nfcidLen<8||block>0xFF)return false;uint8_t body[15]={0x06};memcpy(body+1,tag.nfcid,8);body[9]=1;body[10]=(uint8_t)serviceCode;body[11]=(uint8_t)(serviceCode>>8);body[12]=1;body[13]=0x80;body[14]=(uint8_t)block;uint8_t rx[64]={};size_t n=0;if(!felicaExchange(dev,body,sizeof(body),rx,sizeof(rx),n)||n<29||rx[1]!=0x07||rx[10]||rx[11]||rx[12]!=1)return false;memcpy(data,rx+13,16);return true;}
bool felicaWriteBlock(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint16_t serviceCode,uint16_t block,const uint8_t data[16]){if(tag.nfcidLen<8||block>0xFF)return false;uint8_t body[31]={0x08};memcpy(body+1,tag.nfcid,8);body[9]=1;body[10]=(uint8_t)serviceCode;body[11]=(uint8_t)(serviceCode>>8);body[12]=1;body[13]=0x80;body[14]=(uint8_t)block;memcpy(body+15,data,16);uint8_t rx[32]={};size_t n=0;return felicaExchange(dev,body,sizeof(body),rx,sizeof(rx),n)&&n>=12&&rx[1]==0x09&&rx[10]==0&&rx[11]==0;}

bool felicaReadNdef(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,uint8_t*out,size_t outMax,size_t&outLen,size_t&capacity){outLen=0;capacity=0;uint8_t attr[16]={};if(!felicaReadBlock(dev,tag,0x000B,0,attr))return false;if(attrChecksum(attr)!=((uint16_t)attr[14]<<8|attr[15]))return false;size_t nmax=((size_t)attr[3]<<8)|attr[4];capacity=nmax*16U;size_t len=((size_t)attr[11]<<16)|((size_t)attr[12]<<8)|attr[13];if(len>capacity||len>outMax)return false;size_t off=0;for(uint16_t b=1;off<len;++b){uint8_t d[16];if(!felicaReadBlock(dev,tag,0x000B,b,d))return false;size_t take=min((size_t)16,len-off);memcpy(out+off,d,take);off+=take;}outLen=len;return true;}
bool felicaWriteNdef(ST25R3916Backend& dev,const ST25R3916Backend::ScanResult& tag,const uint8_t*ndef,size_t ndefLen,size_t&capacity){capacity=0;uint8_t attr[16]={};if(!felicaReadBlock(dev,tag,0x0009,0,attr))return false;if(attrChecksum(attr)!=((uint16_t)attr[14]<<8|attr[15])||attr[10]!=0x01)return false;size_t nmax=((size_t)attr[3]<<8)|attr[4];capacity=nmax*16U;if(ndefLen>capacity)return false;uint8_t work[16];memcpy(work,attr,16);work[9]=0x0F;work[11]=work[12]=work[13]=0;uint16_t sum=attrChecksum(work);work[14]=(uint8_t)(sum>>8);work[15]=(uint8_t)sum;if(!felicaWriteBlock(dev,tag,0x0009,0,work))return false;size_t off=0;for(uint16_t b=1;off<ndefLen;++b){memset(work,0,16);size_t take=min((size_t)16,ndefLen-off);memcpy(work,ndef+off,take);if(!felicaWriteBlock(dev,tag,0x0009,b,work))return false;off+=take;}memcpy(work,attr,16);work[9]=0;work[11]=(uint8_t)(ndefLen>>16);work[12]=(uint8_t)(ndefLen>>8);work[13]=(uint8_t)ndefLen;sum=attrChecksum(work);work[14]=(uint8_t)(sum>>8);work[15]=(uint8_t)sum;return felicaWriteBlock(dev,tag,0x0009,0,work);}

} // namespace ST25R3916Experimental
#endif
