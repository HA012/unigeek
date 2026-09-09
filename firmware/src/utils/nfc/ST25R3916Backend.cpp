#include "ST25R3916Backend.h"

#if defined(DEVICE_HAS_ST25R3916)

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>

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

bool ST25R3916Backend::scan(uint16_t techMask, ScanResult& result, uint32_t timeoutMs) {
  result = ScanResult{};
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

      _nfc->rfalNfcDeactivate(false);
      _lastScanCode = ST_ERR_NONE;
      return true;
    }
    delay(5);
  }

  _nfc->rfalNfcDeactivate(false);
  return false;
}

void ST25R3916Backend::end() {
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
