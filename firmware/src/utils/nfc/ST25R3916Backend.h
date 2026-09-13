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
  bool mifareClassicWriteBlock(uint8_t block, const uint8_t data[16]);

  // NFC Forum Type 2 / MIFARE Ultralight / NTAG primitives. scan(...,
  // keepActive=true) must have activated an NFC-A Type 2 tag first.
  bool type2Transceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                       size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 20);
  bool type2ReadPages(uint8_t startPage, uint8_t data[16]);
  bool type2WritePage(uint8_t page, const uint8_t data[4]);
  bool type2WritePageRfalFirst(uint8_t page, const uint8_t data[4]);
  bool type2PwdAuth(const uint8_t pwd[4], uint8_t pack[2] = nullptr);
  uint16_t lastPwdAuthCode() const { return _lastPwdAuthCode; }
  uint16_t lastPwdAuthBits() const { return _lastPwdAuthBits; }
  uint8_t lastType2WriteAckNibble() const { return _lastType2WriteAckNibble; }
  uint16_t lastType2WriteBits() const { return _lastType2WriteBits; }
  uint16_t lastType2WriteCode() const { return _lastType2WriteCode; }

  // NFC-A passive-target emulation. The dump buffer remains owned by the
  // caller and must stay valid while emulation is active.
  bool startType2Emulation(const ScanResult& identity, uint8_t* dump, size_t dumpLen);
  // MIFARE Classic card emulation is limited to the passive-target identity
  // (UID/ATQA/SAK). ST25R3916 cannot expose manual parity in card-emulation
  // mode, so Crypto1 sector emulation is not implemented here.
  bool startMfcUidEmulation(const ScanResult& identity);
  bool emulationWorker();
  void stopEmulation();
  bool emulationActive() const { return _emulating; }

  // Raw NFC-A helpers used by ST25-specific diagnostics such as Magic-card
  // detection. The caller is responsible for framing semantics.
  bool nfcATransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                      size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 20);
  bool nfcATransceiveBits(const uint8_t* tx, size_t txBits, uint8_t* rx,
                          size_t rxMaxBits, size_t& rxBits, uint32_t timeoutMs = 20);

  // Generic helpers used by the experimental NFC-B/F/V and ISO-DEP tools.
  // activeRfTransceive() is for byte-oriented RF frames while the device is
  // active; isoDepTransceive() lets the RFAL high layer handle ISO14443-4
  // framing/chaining for Type 4A/4B APDUs.
  bool activeRfTransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                          size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 50);
  bool isoDepTransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                        size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 1200);

  const Info& info() const { return _info; }
  uint16_t lastScanCode() const { return _lastScanCode; }
  Transport transport() const { return _transport; }
  const ScanResult& activeTag() const { return _activeTag; }
  bool hasActiveTag() const { return _active; }

private:
  bool _finishBegin();
  bool _transceiveBytes(const uint8_t* tx, size_t txLen, uint8_t* rx,
                        size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 20);
  bool _transceivePacked(const uint8_t* txPacked, size_t txBits, uint8_t* rxPacked,
                         size_t rxMaxBits, size_t& rxBits, uint32_t timeoutMs = 8,
                         bool autoTxParity = false);
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
  uint16_t _lastPwdAuthCode = 0xFFFF;
  uint16_t _lastPwdAuthBits = 0;
  uint8_t _lastType2WriteAckNibble = 0xFF;
  uint16_t _lastType2WriteBits = 0;
  uint16_t _lastType2WriteCode = 0xFFFF;
  ScanResult _activeTag;
  bool _active = false;
  Crypto1State* _crypto = nullptr;

  bool _startNfcaListen(const ScanResult& identity);
  bool _listenRespond(const uint8_t* data, uint16_t len, bool withCrc = true);

  bool _emulating = false;
  bool _emuMfc = false;
  uint8_t* _emuDump = nullptr;
  size_t _emuDumpLen = 0;
  ScanResult _emuIdentity;
};

#endif
