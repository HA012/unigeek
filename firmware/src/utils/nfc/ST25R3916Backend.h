#pragma once

#include <Arduino.h>

#if defined(DEVICE_HAS_ST25R3916)
#include <SPI.h>
#include <Wire.h>

class RfalRfST25R3916Class;
class RfalNfcClass;

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
  bool scan(uint16_t techMask, ScanResult& result, uint32_t timeoutMs = 1500);

  const Info& info() const { return _info; }
  uint16_t lastScanCode() const { return _lastScanCode; }
  Transport transport() const { return _transport; }

private:
  bool _finishBegin();

  Transport _transport = Transport::I2C;
  Info _info;
  RfalRfST25R3916Class* _hw = nullptr;
  RfalNfcClass* _nfc = nullptr;
  uint16_t _lastScanCode = 0xFFFF;
};

#endif
