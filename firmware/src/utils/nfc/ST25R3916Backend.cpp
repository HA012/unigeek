#include "ST25R3916Backend.h"

#if defined(DEVICE_HAS_ST25R3916)

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>

#include "utils/crypto/crapto1.h"

namespace {
uint32_t bytesToU32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void setPackedBit(uint8_t* buf, size_t bit, uint8_t value) {
  if (value) buf[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
}

uint8_t getPackedBit(const uint8_t* buf, size_t bit) {
  return (uint8_t)((buf[bit >> 3] >> (bit & 7U)) & 1U);
}
}

ST25R3916Backend::~ST25R3916Backend() {
  end();
}

bool ST25R3916Backend::beginI2C(TwoWire* wire, uint8_t address) {
  end();
  _transport = Transport::I2C;
  if (!wire) return false;

  wire->beginTransmission(address);
  _info.busDetected = (wire->endTransmission() == 0);
  if (!_info.busDetected) return false;

  // The M5Stack U216 exposes no ST25R IRQ on the Grove connector. Passing -1
  // selects polling mode enabled by scripts/patch_st25r3916.py.
  _hw = new RfalRfST25R3916Class(wire, -1);
  return _finishBegin();
}

bool ST25R3916Backend::beginSPI(SPIClass* spi, int csPin, int irqPin, uint32_t spiHz) {
  end();
  _transport = Transport::SPI;
  if (!spi || csPin < 0 || irqPin < 0) return false;

  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);
  _info.busDetected = true;

  _hw = new RfalRfST25R3916Class(spi, csPin, irqPin, spiHz);
  return _finishBegin();
}

bool ST25R3916Backend::_finishBegin() {
  if (!_hw) return false;

  _nfc = new RfalNfcClass(_hw);
  if (!_nfc) return false;

  _info.initCode = _nfc->rfalNfcInitialize();
  _info.initialized = (_info.initCode == ST_ERR_NONE);
  if (!_info.initialized) return false;

  _hw->st25r3916OscOn();
  if (_hw->st25r3916ReadRegister(ST25R3916_REG_IC_IDENTITY, &_info.chipId) != ST_ERR_NONE) {
    _info.initialized = false;
    return false;
  }
  _info.is3916 = _hw->st25r3916ChipIsST25R3916();
  _info.is3916B = _hw->st25r3916ChipIsST25R3916B();
  return true;
}

bool ST25R3916Backend::scan(uint16_t techMask, ScanResult& result, uint32_t timeoutMs,
                            bool keepActive) {
  result = ScanResult{};
  _activeTag = ScanResult{};
  _active = false;
  if (_crypto) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }
  _lastScanCode = 0xFFFF;
  if (!_nfc || !_hw || !_info.initialized || techMask == 0) return false;

  rfalNfcDiscoverParam params;
  memset(&params, 0, sizeof(params));
  params.compMode = RFAL_COMPLIANCE_MODE_NFC;
  params.devLimit = 1;
  params.nfcfBR = RFAL_BR_212;
  params.ap2pBR = RFAL_BR_424;
  params.techs2Find = techMask;
  params.GBLen = RFAL_NFCDEP_GB_MAX_LEN;
  params.totalDuration = (uint16_t)((timeoutMs > 0xFFFFU) ? 0xFFFFU : timeoutMs);
  params.wakeupEnabled = false;
  params.wakeupConfigDefault = true;

  _lastScanCode = _nfc->rfalNfcDiscover(&params);
  if (_lastScanCode != ST_ERR_NONE) return false;

  _hw->st25r3916OscOn();
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < timeoutMs) {
    _nfc->rfalNfcWorker();
    if (_nfc->rfalNfcGetState() == RFAL_NFC_STATE_ACTIVATED) {
      rfalNfcDevice* dev = nullptr;
      _lastScanCode = _nfc->rfalNfcGetActiveDevice(&dev);
      if (_lastScanCode != ST_ERR_NONE || !dev) break;

      switch (dev->type) {
        case RFAL_NFC_LISTEN_TYPE_NFCA: result.technology = Technology::NFC_A; break;
        case RFAL_NFC_LISTEN_TYPE_NFCB: result.technology = Technology::NFC_B; break;
        case RFAL_NFC_LISTEN_TYPE_NFCF: result.technology = Technology::NFC_F; break;
        case RFAL_NFC_LISTEN_TYPE_NFCV: result.technology = Technology::NFC_V; break;
        default: result.technology = Technology::UNKNOWN; break;
      }

      result.nfcidLen = (dev->nfcidLen < sizeof(result.nfcid)) ? dev->nfcidLen : sizeof(result.nfcid);
      if (dev->nfcid && result.nfcidLen) memcpy(result.nfcid, dev->nfcid, result.nfcidLen);
      result.isoDep = (dev->rfInterface == RFAL_NFC_INTERFACE_ISODEP);
      if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCA) {
        result.sak = dev->dev.nfca.selRes.sak;
        result.atqa[0] = dev->dev.nfca.sensRes.anticollisionInfo;
        result.atqa[1] = dev->dev.nfca.sensRes.platformInfo;
      }

      _lastScanCode = ST_ERR_NONE;
      if (keepActive) {
        _activeTag = result;
        _active = true;
      } else {
        deactivate();
      }
      return true;
    }
    delay(5);
  }

  deactivate();
  return false;
}

void ST25R3916Backend::deactivate() {
  if (_crypto) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }
  _active = false;
  _activeTag = ScanResult{};
  if (!_nfc) return;

  _nfc->rfalNfcDeactivate(false);
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < 80U) {
    _nfc->rfalNfcWorker();
    if (_nfc->rfalNfcGetState() == RFAL_NFC_STATE_IDLE) break;
    delay(1);
  }
}

uint8_t ST25R3916Backend::_oddParity(uint8_t value) {
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;
  return (uint8_t)((~value) & 1U);
}

uint16_t ST25R3916Backend::_crcA(const uint8_t* data, size_t len) {
  uint16_t crc = 0x6363U;
  while (len--) {
    uint8_t d = (uint8_t)(*data++ ^ (crc & 0x00FFU));
    d ^= (uint8_t)(d << 4);
    crc = (uint16_t)((crc >> 8) ^ ((uint16_t)d << 8) ^ ((uint16_t)d << 3) ^ ((uint16_t)d >> 4));
  }
  return crc;
}

uint64_t ST25R3916Backend::_key48(const uint8_t key[6]) {
  uint64_t value = 0;
  for (uint8_t i = 0; i < 6; ++i) value = (value << 8) | key[i];
  return value;
}

uint32_t ST25R3916Backend::_uid32(const ScanResult& tag) {
  if (tag.nfcidLen < 4) return 0;
  const uint8_t* p = &tag.nfcid[tag.nfcidLen - 4];
  return bytesToU32(p);
}

bool ST25R3916Backend::_transceiveBytes(const uint8_t* tx, size_t txLen,
                                         uint8_t* rx, size_t rxMaxLen,
                                         size_t& rxLen, uint32_t timeoutMs) {
  rxLen = 0;
  if (!_hw || !tx || !txLen || !rx || !rxMaxLen) return false;

  uint16_t receivedBits = 0;
  rfalTransceiveContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.txBuf = const_cast<uint8_t*>(tx);
  ctx.txBufLen = (uint16_t)(txLen * 8U);
  ctx.rxBuf = rx;
  ctx.rxBufLen = (uint16_t)(rxMaxLen * 8U);
  ctx.rxRcvdLen = &receivedBits;
  ctx.flags = RFAL_TXRX_FLAGS_DEFAULT;
  ctx.fwt = rfalConvMsTo1fc(timeoutMs);

  ReturnCode rc = _hw->rfalStartTransceive(&ctx);
  if (rc != ST_ERR_NONE) return false;
  do {
    _hw->rfalWorker();
    rc = _hw->rfalGetTransceiveStatus();
  } while (rc == ST_ERR_BUSY);
  if (rc != ST_ERR_NONE || receivedBits == 0 || (receivedBits & 7U)) return false;

  rxLen = receivedBits / 8U;
  if (rxLen > rxMaxLen) rxLen = rxMaxLen;
  return true;
}

bool ST25R3916Backend::_transceivePacked(const uint8_t* txPacked, size_t txBits,
                                            uint8_t* rxPacked, size_t rxMaxBits,
                                            size_t& rxBits, uint32_t timeoutMs) {
  rxBits = 0;
  if (!_hw || !txPacked || !txBits || !rxPacked || !rxMaxBits) return false;

  uint16_t receivedBits = 0;
  rfalTransceiveContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.txBuf = const_cast<uint8_t*>(txPacked);
  ctx.txBufLen = (uint16_t)txBits;
  ctx.rxBuf = rxPacked;
  ctx.rxBufLen = (uint16_t)rxMaxBits;
  ctx.rxRcvdLen = &receivedBits;
  ctx.flags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
              RFAL_TXRX_FLAGS_CRC_RX_MANUAL |
              RFAL_TXRX_FLAGS_CRC_RX_KEEP |
              RFAL_TXRX_FLAGS_PAR_RX_KEEP |
              RFAL_TXRX_FLAGS_PAR_TX_NONE |
              RFAL_TXRX_FLAGS_AGC_ON;
  ctx.fwt = rfalConvMsTo1fc(timeoutMs);

  ReturnCode rc = _hw->rfalStartTransceive(&ctx);
  if (rc != ST_ERR_NONE) return false;
  do {
    _hw->rfalWorker();
    rc = _hw->rfalGetTransceiveStatus();
  } while (rc == ST_ERR_BUSY);
  if (rc != ST_ERR_NONE || receivedBits == 0) return false;
  rxBits = receivedBits;
  return true;
}

bool ST25R3916Backend::_transceiveRaw9(const uint8_t* txData, const uint8_t* txParity,
                                       size_t txLen, uint8_t* rxData, uint8_t* rxParity,
                                       size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs) {
  rxLen = 0;
  if (!txData || !txParity || !txLen || !rxData || !rxParity || !rxMaxLen) return false;
  if (txLen > 24 || rxMaxLen > 24) return false;

  uint8_t txPacked[27] = {};
  uint8_t rxPacked[27] = {};
  const size_t txBits = txLen * 9U;
  const size_t rxMaxBits = rxMaxLen * 9U;

  for (size_t i = 0; i < txLen; ++i) {
    const size_t base = i * 9U;
    for (uint8_t bit = 0; bit < 8; ++bit) setPackedBit(txPacked, base + bit, (txData[i] >> bit) & 1U);
    setPackedBit(txPacked, base + 8U, txParity[i] & 1U);
  }

  size_t rxBits = 0;
  if (!_transceivePacked(txPacked, txBits, rxPacked, rxMaxBits, rxBits, timeoutMs) || rxBits < 9U) {
    return false;
  }

  rxLen = rxBits / 9U;
  if (rxLen > rxMaxLen) rxLen = rxMaxLen;
  for (size_t i = 0; i < rxLen; ++i) {
    const size_t base = i * 9U;
    uint8_t value = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) value |= (uint8_t)(getPackedBit(rxPacked, base + bit) << bit);
    rxData[i] = value;
    rxParity[i] = getPackedBit(rxPacked, base + 8U);
  }
  return true;
}

bool ST25R3916Backend::mifareClassicAuthenticate(uint8_t block, const uint8_t key[6], bool keyB) {
  if (!_active || _activeTag.technology != Technology::NFC_A || !_hw || !key) return false;
  if (_crypto) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }

  uint8_t cmd[4] = {(uint8_t)(keyB ? 0x61 : 0x60), block, 0, 0};
  const uint16_t authCrc = _crcA(cmd, 2);
  cmd[2] = (uint8_t)(authCrc & 0xFFU);
  cmd[3] = (uint8_t)(authCrc >> 8);
  uint8_t cmdPar[4] = {};
  for (uint8_t i = 0; i < sizeof(cmd); ++i) cmdPar[i] = _oddParity(cmd[i]);

  // The Classic authentication nonce is a four-byte frame without a CRC.
  // Use explicit ISO14443A parity from the first exchange onward so RFAL does
  // not try to CRC-check the nonce response.
  uint8_t nonceBytes[4] = {};
  uint8_t noncePar[4] = {};
  size_t nonceLen = 0;
  if (!_transceiveRaw9(cmd, cmdPar, sizeof(cmd), nonceBytes, noncePar,
                       sizeof(nonceBytes), nonceLen) || nonceLen != 4) {
    return false;
  }
  for (uint8_t i = 0; i < sizeof(nonceBytes); ++i) {
    if (noncePar[i] != _oddParity(nonceBytes[i])) return false;
  }

  const uint32_t nt = bytesToU32(nonceBytes);
  _crypto = crypto1_create(_key48(key));
  if (!_crypto) return false;
  crypto1_word(_crypto, nt ^ _uid32(_activeTag), 0);

  uint8_t plain[8] = {};
  uint8_t enc[8] = {};
  uint8_t par[8] = {};
  uint32_t ntp = prng_successor(nt, 32);

  // A zero reader nonce is sufficient for normal mutual authentication and
  // matches the existing UniGeek Crypto1 attack implementation.
  for (uint8_t i = 0; i < 4; ++i) {
    enc[i] = crypto1_byte(_crypto, plain[i], 0) ^ plain[i];
    par[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
  }
  for (uint8_t i = 4; i < 8; ++i) {
    ntp = prng_successor(ntp, 8);
    plain[i] = (uint8_t)(ntp & 0xFFU);
    enc[i] = crypto1_byte(_crypto, plain[i], 0) ^ plain[i];
    par[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
  }

  uint8_t answer[4] = {};
  uint8_t answerPar[4] = {};
  size_t answerLen = 0;
  if (!_transceiveRaw9(enc, par, sizeof(enc), answer, answerPar, sizeof(answer), answerLen) || answerLen != 4) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
    return false;
  }

  const uint32_t encryptedAt = bytesToU32(answer);
  const uint32_t expectedAt = prng_successor(ntp, 32);
  const uint32_t at = crypto1_word(_crypto, 0, 0) ^ encryptedAt;
  if (at != expectedAt) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
    return false;
  }
  return true;
}

bool ST25R3916Backend::mifareClassicReadBlock(uint8_t block, uint8_t data[16]) {
  if (!_active || !_crypto || !data) return false;

  uint8_t plain[4] = {0x30, block, 0, 0};
  const uint16_t crc = _crcA(plain, 2);
  plain[2] = (uint8_t)(crc & 0xFFU);
  plain[3] = (uint8_t)(crc >> 8);

  uint8_t enc[4] = {};
  uint8_t par[4] = {};
  for (uint8_t i = 0; i < sizeof(plain); ++i) {
    enc[i] = crypto1_byte(_crypto, 0, 0) ^ plain[i];
    par[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
  }

  uint8_t response[18] = {};
  uint8_t responsePar[18] = {};
  size_t responseLen = 0;
  if (!_transceiveRaw9(enc, par, sizeof(enc), response, responsePar,
                       sizeof(response), responseLen) || responseLen != sizeof(response)) {
    return false;
  }

  uint8_t decoded[18] = {};
  for (uint8_t i = 0; i < sizeof(decoded); ++i) {
    decoded[i] = response[i] ^ crypto1_byte(_crypto, 0, 0);
    const uint8_t decodedParity = (uint8_t)(filter(_crypto->odd) ^ responsePar[i]);
    if (decodedParity != _oddParity(decoded[i])) return false;
  }
  const uint16_t expectedCrc = _crcA(decoded, 16);
  const uint16_t gotCrc = (uint16_t)decoded[16] | ((uint16_t)decoded[17] << 8);
  if (expectedCrc != gotCrc) return false;

  memcpy(data, decoded, 16);
  return true;
}

bool ST25R3916Backend::mifareClassicWriteBlock(uint8_t block, const uint8_t data[16]) {
  if (!_active || !_crypto || !data) return false;

  auto sendEncryptedFrame = [&](const uint8_t* plain, size_t len) -> bool {
    uint8_t enc[18] = {};
    uint8_t par[18] = {};
    uint8_t txPacked[21] = {};
    uint8_t rxPacked[2] = {};
    for (size_t i = 0; i < len; ++i) {
      enc[i] = crypto1_byte(_crypto, 0, 0) ^ plain[i];
      par[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
      const size_t base = i * 9U;
      for (uint8_t bit = 0; bit < 8; ++bit) setPackedBit(txPacked, base + bit, (enc[i] >> bit) & 1U);
      setPackedBit(txPacked, base + 8U, par[i]);
    }

    size_t rxBits = 0;
    if (!_transceivePacked(txPacked, len * 9U, rxPacked, 4U, rxBits) || rxBits != 4U) return false;
    uint8_t ack = 0;
    for (uint8_t bit = 0; bit < 4; ++bit) {
      const uint8_t encryptedBit = getPackedBit(rxPacked, bit);
      const uint8_t plainBit = (uint8_t)(encryptedBit ^ crypto1_bit(_crypto, 0, 0));
      ack |= (uint8_t)(plainBit << bit);
    }
    return ack == 0x0AU;
  };

  uint8_t cmd[4] = {0xA0, block, 0, 0};
  const uint16_t cmdCrc = _crcA(cmd, 2);
  cmd[2] = (uint8_t)(cmdCrc & 0xFFU);
  cmd[3] = (uint8_t)(cmdCrc >> 8);
  if (!sendEncryptedFrame(cmd, sizeof(cmd))) return false;

  uint8_t payload[18] = {};
  memcpy(payload, data, 16);
  const uint16_t dataCrc = _crcA(payload, 16);
  payload[16] = (uint8_t)(dataCrc & 0xFFU);
  payload[17] = (uint8_t)(dataCrc >> 8);
  return sendEncryptedFrame(payload, sizeof(payload));
}

bool ST25R3916Backend::type2Transceive(const uint8_t* tx, size_t txLen,
                                          uint8_t* rx, size_t rxMaxLen,
                                          size_t& rxLen, uint32_t timeoutMs) {
  if (!_active || _activeTag.technology != Technology::NFC_A) {
    rxLen = 0;
    return false;
  }
  return _transceiveBytes(tx, txLen, rx, rxMaxLen, rxLen, timeoutMs);
}

bool ST25R3916Backend::type2ReadPages(uint8_t startPage, uint8_t data[16]) {
  if (!data) return false;
  const uint8_t cmd[2] = {0x30, startPage};
  size_t rxLen = 0;
  memset(data, 0, 16);
  return type2Transceive(cmd, sizeof(cmd), data, 16, rxLen, 20) && rxLen == 16;
}

bool ST25R3916Backend::type2WritePage(uint8_t page, const uint8_t data[4]) {
  if (!_active || _activeTag.technology != Technology::NFC_A || !_hw || !data) return false;

  uint8_t cmd[6] = {0xA2, page, data[0], data[1], data[2], data[3]};
  uint8_t rx[1] = {};
  uint16_t receivedBits = 0;
  rfalTransceiveContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.txBuf = cmd;
  ctx.txBufLen = (uint16_t)(sizeof(cmd) * 8U);
  ctx.rxBuf = rx;
  ctx.rxBufLen = 4U;
  ctx.rxRcvdLen = &receivedBits;
  ctx.flags = RFAL_TXRX_FLAGS_DEFAULT;
  ctx.fwt = rfalConvMsTo1fc(20);

  ReturnCode rc = _hw->rfalStartTransceive(&ctx);
  if (rc != ST_ERR_NONE) return false;
  do {
    _hw->rfalWorker();
    rc = _hw->rfalGetTransceiveStatus();
  } while (rc == ST_ERR_BUSY);

  return rc == ST_ERR_NONE && receivedBits == 4U && (rx[0] & 0x0FU) == 0x0AU;
}

void ST25R3916Backend::end() {
  if (_crypto) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }
  _active = false;
  _activeTag = ScanResult{};

  if (_hw && _info.initialized) {
    _hw->rfalFieldOff();
    _hw->rfalDeinitialize();
  }

  delete _nfc;
  _nfc = nullptr;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
  delete _hw;
#pragma GCC diagnostic pop
  _hw = nullptr;

  _info = Info{};
}

#endif
