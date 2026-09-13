#pragma once

#include <Arduino.h>

#if defined(DEVICE_HAS_ST25R3916)
#include "utils/nfc/ST25R3916Backend.h"

namespace ST25R3916Experimental {

struct TypeVInfo {
  uint16_t blocks = 0;
  uint8_t blockSize = 0;
  uint8_t dsfid = 0;
  uint8_t afi = 0;
  bool hasDsfid = false;
  bool hasAfi = false;
};

bool desfireExchange(ST25R3916Backend& dev, uint8_t ins, const uint8_t* data, size_t dataLen,
                     uint8_t* out, size_t outMax, size_t& outLen, uint8_t& status);
bool desfireProbe(ST25R3916Backend& dev);
bool desfireAuthenticateAes(ST25R3916Backend& dev, uint8_t keyNo, const uint8_t key[16]);

bool type4ReadNdef(ST25R3916Backend& dev, uint8_t* out, size_t outMax, size_t& outLen, size_t& capacity);
bool type4WriteNdef(ST25R3916Backend& dev, const uint8_t* ndef, size_t ndefLen, size_t& capacity);

bool typeVGetSystemInfo(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag, TypeVInfo& info);
bool typeVReadBlock(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                    uint16_t block, uint8_t* out, size_t outMax, size_t& outLen);
bool typeVWriteBlock(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                     uint16_t block, const uint8_t* data, size_t dataLen);
bool typeVLockBlock(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag, uint16_t block);
bool typeVSecurityStatus(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                         uint16_t block, uint8_t& status);
bool typeVReadNdef(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                   uint8_t* out, size_t outMax, size_t& outLen, size_t& capacity);
bool typeVWriteNdef(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                    const uint8_t* ndef, size_t ndefLen, size_t& capacity, bool allowFormat);
bool typeVEraseTagSafe(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                       size_t& capacity);
bool typeVPresentIcodePassword(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                               uint8_t passwordId, const uint8_t password[4]);
bool typeVPresentStPassword(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                            uint8_t passwordId, const uint8_t* password, size_t passwordLen);

bool felicaRequestSystemCodes(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                              uint16_t* systems, size_t maxSystems, size_t& count);
bool felicaSearchService(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                         uint16_t index, uint16_t& serviceCode);
bool felicaRequestService(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                          uint16_t serviceCode, uint16_t& keyVersion);
bool felicaReadBlock(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                     uint16_t serviceCode, uint16_t block, uint8_t data[16]);
bool felicaWriteBlock(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                      uint16_t serviceCode, uint16_t block, const uint8_t data[16]);
bool felicaReadNdef(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                    uint8_t* out, size_t outMax, size_t& outLen, size_t& capacity);
bool felicaWriteNdef(ST25R3916Backend& dev, const ST25R3916Backend::ScanResult& tag,
                     const uint8_t* ndef, size_t ndefLen, size_t& capacity);

}  // namespace ST25R3916Experimental
#endif
