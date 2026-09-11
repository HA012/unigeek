#include "ST25R3916Backend.h"

#if defined(DEVICE_HAS_ST25R3916)

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <esp_random.h>

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

uint16_t mfcPackBits(const uint8_t* data, const uint8_t* parity, size_t count, uint8_t* out) {
  uint16_t bit = 0;
  for (size_t i = 0; i < count; ++i) {
    for (uint8_t b = 0; b < 8; ++b, ++bit)
      if (data[i] & (1U << b)) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
    if (parity[i] & 1U) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
    ++bit;
  }
  return bit;
}

uint8_t mfcUnpackBits(const uint8_t* in, uint16_t nbits, uint8_t* out, uint8_t maxBytes) {
  uint8_t n = 0;
  uint16_t bit = 0;
  while ((bit + 8U) <= nbits && n < maxBytes) {
    uint8_t value = 0;
    for (uint8_t b = 0; b < 8; ++b, ++bit)
      if (in[bit >> 3] & (1U << (bit & 7U))) value |= (uint8_t)(1U << b);
    out[n++] = value;
    if (bit < nbits) ++bit;
  }
  return n;
}

bool mfcRxOk(ReturnCode rc) {
  return rc == ST_ERR_NONE ||
         (rc >= ST_ERR_INCOMPLETE_BYTE && rc <= ST_ERR_INCOMPLETE_BYTE_07);
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
  // PAR_RX_KEEP disables RFAL's automatic parity/CRC checking and keeps the
  // raw parity/CRC bits in the receive FIFO.  This fork does not define the
  // newer RFAL_TXRX_FLAGS_CRC_RX_MANUAL alias, so do not depend on it here.
  ctx.flags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
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

  // Raw ISO14443A/MIFARE exchanges intentionally bypass the normal CRC and
  // byte-framing machinery. RFAL may therefore report CRC or incomplete-byte
  // status even though the response bits in the FIFO are valid and expected
  // (for example, a 4-bit MIFARE ACK). Treat those statuses as data-bearing
  // completions whenever bits were actually received.
  const uint16_t err = (uint16_t)(rc & 0x00FFU);
  const bool dataBearingStatus =
      (rc == ST_ERR_NONE) || err == 21U /* ERR_CRC */ ||
      (err >= 41U && err <= 47U) /* ERR_INCOMPLETE_BYTE_01..07 */;
  if (!dataBearingStatus || receivedBits == 0) return false;
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

static ReturnCode mifareTransceiveRaw(RfalRfST25R3916Class* hw, uint8_t* txBuf, uint16_t txBits,
                                      uint8_t* rxBuf, uint16_t rxCapBytes, uint16_t* rxBits,
                                      uint32_t fwt, uint32_t flags) {
  if (!hw || !txBuf || !txBits || !rxBuf || !rxCapBytes || !rxBits) return ST_ERR_PARAM;
  rfalTransceiveContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.txBuf = txBuf;
  ctx.txBufLen = txBits;
  ctx.rxBuf = rxBuf;
  ctx.rxBufLen = (uint16_t)rfalConvBytesToBits(rxCapBytes);
  ctx.rxRcvdLen = rxBits;
  ctx.flags = flags;
  ctx.fwt = fwt;
  *rxBits = 0;
  ReturnCode rc = hw->rfalStartTransceive(&ctx);
  if (rc != ST_ERR_NONE) return rc;
  const uint32_t started = millis();
  do {
    hw->rfalWorker();
    rc = hw->rfalGetTransceiveStatus();
  } while (rc == ST_ERR_BUSY && millis() - started < 60U);
  return rc;
}

bool ST25R3916Backend::mifareClassicAuthenticate(uint8_t block, const uint8_t key[6], bool keyB) {
  if (!_active || _activeTag.technology != Technology::NFC_A || !_hw || !key) return false;

  const uint32_t encFlags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
                            RFAL_TXRX_FLAGS_CRC_RX_KEEP |
                            RFAL_TXRX_FLAGS_NFCIP1_OFF |
                            RFAL_TXRX_FLAGS_AGC_ON |
                            RFAL_TXRX_FLAGS_PAR_RX_KEEP |
                            RFAL_TXRX_FLAGS_PAR_TX_NONE;

  uint8_t cmd[2] = {(uint8_t)(keyB ? 0x61 : 0x60), block};
  uint32_t nt = 0;

  if (!_crypto) {
    // First authentication: clear AUTH command with manual CRC and automatic
    // parity. The nonce response has no CRC and arrives with parity retained.
    const uint16_t crc = _hw->rfalCrcCalculateCcitt(0x6363, cmd, 2);
    uint8_t tx[4] = {cmd[0], cmd[1], (uint8_t)(crc & 0xFFU), (uint8_t)(crc >> 8)};
    uint8_t rx[8] = {};
    uint16_t rxBits = 0;
    const uint32_t firstFlags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
                                RFAL_TXRX_FLAGS_CRC_RX_KEEP |
                                RFAL_TXRX_FLAGS_NFCIP1_OFF |
                                RFAL_TXRX_FLAGS_AGC_ON |
                                RFAL_TXRX_FLAGS_PAR_RX_KEEP |
                                RFAL_TXRX_FLAGS_PAR_TX_AUTO;
    ReturnCode rc = mifareTransceiveRaw(_hw, tx, rfalConvBytesToBits(sizeof(tx)), rx, sizeof(rx),
                                        &rxBits, rfalConvMsTo1fc(20), firstFlags);
    if (!mfcRxOk(rc) || rxBits < 32U) return false;

    uint8_t ntBytes[4] = {};
    if (mfcUnpackBits(rx, rxBits, ntBytes, sizeof(ntBytes)) < 4U) return false;
    nt = bytesToU32(ntBytes);
  } else {
    // Nested authentication: while Crypto1 is active, AUTH itself is an
    // encrypted MIFARE frame. This avoids a full RF deactivate/reselect for
    // every sector when the next key is known.
    const uint16_t crc = _hw->rfalCrcCalculateCcitt(0x6363, cmd, 2);
    uint8_t plain[4] = {cmd[0], cmd[1], (uint8_t)(crc & 0xFFU), (uint8_t)(crc >> 8)};
    uint8_t enc[4] = {}, parity[4] = {};
    for (uint8_t i = 0; i < 4; ++i) {
      enc[i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ plain[i]);
      parity[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
    }
    uint8_t packed[8] = {}, rx[8] = {};
    const uint16_t txBits = mfcPackBits(enc, parity, sizeof(enc), packed);
    uint16_t rxBits = 0;
    ReturnCode rc = mifareTransceiveRaw(_hw, packed, txBits, rx, sizeof(rx), &rxBits,
                                        rfalConvMsTo1fc(20), encFlags);
    if (!mfcRxOk(rc) || rxBits < 32U) {
      crypto1_destroy(_crypto); _crypto = nullptr;
      return false;
    }

    uint8_t encNt[4] = {};
    if (mfcUnpackBits(rx, rxBits, encNt, sizeof(encNt)) < 4U) {
      crypto1_destroy(_crypto); _crypto = nullptr;
      return false;
    }
    for (uint8_t i = 0; i < 4; ++i)
      nt = (nt << 8) | (uint8_t)(encNt[i] ^ crypto1_byte(_crypto, 0, 0));

    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }

  _crypto = crypto1_create(_key48(key));
  if (!_crypto) return false;
  crypto1_word(_crypto, nt ^ _uid32(_activeTag), 0);

  const uint32_t nr = esp_random();
  uint8_t nrPlain[4] = {(uint8_t)(nr >> 24), (uint8_t)(nr >> 16),
                        (uint8_t)(nr >> 8), (uint8_t)nr};
  uint8_t enc[8] = {}, parity[8] = {};
  for (uint8_t i = 0; i < 4; ++i) {
    enc[i] = (uint8_t)(crypto1_byte(_crypto, nrPlain[i], 0) ^ nrPlain[i]);
    parity[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(nrPlain[i]));
  }
  const uint32_t ar = prng_successor(nt, 64);
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t value = (uint8_t)(ar >> (24U - 8U * i));
    enc[4 + i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ value);
    parity[4 + i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(value));
  }

  uint8_t packed[16] = {}, rx[8] = {};
  const uint16_t txBits = mfcPackBits(enc, parity, sizeof(enc), packed);
  uint16_t rxBits = 0;
  ReturnCode rc = mifareTransceiveRaw(_hw, packed, txBits, rx, sizeof(rx), &rxBits,
                                      rfalConvMsTo1fc(20), encFlags);
  if (!mfcRxOk(rc) || rxBits < 32U) { crypto1_destroy(_crypto); _crypto = nullptr; return false; }

  uint8_t atEnc[4] = {};
  if (mfcUnpackBits(rx, rxBits, atEnc, sizeof(atEnc)) < 4U) {
    crypto1_destroy(_crypto); _crypto = nullptr; return false;
  }
  uint32_t at = 0;
  for (uint8_t i = 0; i < 4; ++i)
    at = (at << 8) | (uint8_t)(atEnc[i] ^ crypto1_byte(_crypto, 0, 0));
  if (at != prng_successor(nt, 96)) {
    crypto1_destroy(_crypto); _crypto = nullptr; return false;
  }
  return true;
}

bool ST25R3916Backend::mifareClassicReadBlock(uint8_t block, uint8_t data[16]) {
  if (!_active || !_crypto || !data) return false;
  uint8_t plain[4] = {0x30, block, 0, 0};
  const uint16_t crc = _hw->rfalCrcCalculateCcitt(0x6363, plain, 2);
  plain[2] = (uint8_t)(crc & 0xFFU); plain[3] = (uint8_t)(crc >> 8);
  uint8_t enc[4] = {}, parity[4] = {};
  for (uint8_t i = 0; i < 4; ++i) {
    enc[i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ plain[i]);
    parity[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
  }
  uint8_t tx[8] = {}, rx[32] = {};
  const uint16_t txBits = mfcPackBits(enc, parity, sizeof(enc), tx);
  uint16_t rxBits = 0;
  const uint32_t flags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
                         RFAL_TXRX_FLAGS_CRC_RX_KEEP |
                         RFAL_TXRX_FLAGS_NFCIP1_OFF |
                         RFAL_TXRX_FLAGS_AGC_ON |
                         RFAL_TXRX_FLAGS_PAR_RX_KEEP |
                         RFAL_TXRX_FLAGS_PAR_TX_NONE;
  ReturnCode rc = mifareTransceiveRaw(_hw, tx, txBits, rx, sizeof(rx), &rxBits,
                                        rfalConvMsTo1fc(20), flags);
  if (!mfcRxOk(rc) || rxBits < 144U) return false;
  uint8_t decoded[18] = {};
  if (mfcUnpackBits(rx, rxBits, decoded, sizeof(decoded)) < 18U) return false;
  for (uint8_t i = 0; i < 18; ++i) decoded[i] ^= crypto1_byte(_crypto, 0, 0);
  const uint16_t expected = _hw->rfalCrcCalculateCcitt(0x6363, decoded, 16);
  const uint16_t got = (uint16_t)decoded[16] | ((uint16_t)decoded[17] << 8);
  if (expected != got) return false;
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

bool ST25R3916Backend::nfcATransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                                        size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs) {
  if (!_active || _activeTag.technology != Technology::NFC_A) { rxLen = 0; return false; }
  return _transceiveBytes(tx, txLen, rx, rxMaxLen, rxLen, timeoutMs);
}

bool ST25R3916Backend::nfcATransceiveBits(const uint8_t* tx, size_t txBits, uint8_t* rx,
                                            size_t rxMaxBits, size_t& rxBits, uint32_t timeoutMs) {
  if (!_active || _activeTag.technology != Technology::NFC_A) { rxBits = 0; return false; }
  return _transceivePacked(tx, txBits, rx, rxMaxBits, rxBits, timeoutMs);
}

bool ST25R3916Backend::type2ReadPages(uint8_t startPage, uint8_t data[16]) {
  if (!data) return false;
  const uint8_t cmd[2] = {0x30, startPage};
  size_t rxLen = 0;
  memset(data, 0, 16);
  return type2Transceive(cmd, sizeof(cmd), data, 16, rxLen, 20) && rxLen == 16;
}

bool ST25R3916Backend::type2WritePage(uint8_t page, const uint8_t data[4]) {
  if (!_active || _activeTag.technology != Technology::NFC_A || !_nfc || !data) return false;
  // Use RFAL's dedicated Type-2 poller primitive, as Bruce does. It handles
  // the 4-bit ACK/NAK framing internally; treating WRITE as a normal byte
  // transceive is unreliable on ST25R3916.
  return _nfc->rfalT2TPollerWrite(page, const_cast<uint8_t*>(data)) == ST_ERR_NONE;
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
