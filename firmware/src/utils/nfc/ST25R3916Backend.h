#pragma once

#include <Arduino.h>

#if defined(DEVICE_HAS_ST25R3916)
#include <SPI.h>
#include <Wire.h>

class RfalRfST25R3916Class;
class RfalNfcClass;
struct Crypto1State;

class ST25R3916Backend {
public:
  enum class Transport : uint8_t { I2C, SPI };

  enum TechMask : uint16_t {
    TECH_A   = 0x0001,
    TECH_B   = 0x0002,
    TECH_F   = 0x0004,
    TECH_V   = 0x0008,
    TECH_ALL = TECH_A | TECH_B | TECH_F | TECH_V,
  };

  enum class Technology : uint8_t { UNKNOWN, NFC_A, NFC_B, NFC_F, NFC_V };

  struct Info {
    bool busDetected = false;
    bool initialized = false;
    uint16_t initCode = 0xFFFF;
    uint8_t chipId = 0;
    bool is3916 = false;
    bool is3916B = false;
  };

  struct ScanResult {
    Technology technology = Technology::UNKNOWN;
    uint8_t nfcid[10] = {};
    uint8_t nfcidLen = 0;
    bool isoDep = false;
    uint8_t sak = 0;
    uint8_t atqa[2] = {};
  };

  ST25R3916Backend() = default;
  ~ST25R3916Backend();

  bool beginI2C(TwoWire* wire, uint8_t address = 0x50);
  bool beginSPI(SPIClass* spi, int csPin, int irqPin, uint32_t spiHz = 5000000);
  void end();
  bool scan(uint16_t techMask, ScanResult& result, uint32_t timeoutMs = 1500,
            bool keepActive = false);
  void deactivate();

  // MIFARE Classic primitives. scan(..., keepActive=true) must have activated
  // an NFC-A Classic tag before authenticate/read are used.
  bool mifareClassicAuthenticate(uint8_t block, const uint8_t key[6], bool keyB = false);
  bool mifareClassicReadBlock(uint8_t block, uint8_t data[16]);

  const Info& info() const { return _info; }
  uint16_t lastScanCode() const { return _lastScanCode; }
  Transport transport() const { return _transport; }
  const ScanResult& activeTag() const { return _activeTag; }
  bool hasActiveTag() const { return _active; }

private:
  bool _finishBegin();
  bool _transceiveRaw9(const uint8_t* txData, const uint8_t* txParity, size_t txLen,
                       uint8_t* rxData, uint8_t* rxParity, size_t rxMaxLen,
                       size_t& rxLen, uint32_t timeoutMs = 8);
  static uint16_t _crcA(const uint8_t* data, size_t len);
  static uint64_t _key48(const uint8_t key[6]);
  static uint32_t _uid32(const ScanResult& tag);
  static uint8_t _oddParity(uint8_t value);

  Transport _transport = Transport::I2C;
  Info _info;
  RfalRfST25R3916Class* _hw = nullptr;
  RfalNfcClass* _nfc = nullptr;
  uint16_t _lastScanCode = 0xFFFF;
  ScanResult _activeTag;
  bool _active = false;
  Crypto1State* _crypto = nullptr;
};

#endif
