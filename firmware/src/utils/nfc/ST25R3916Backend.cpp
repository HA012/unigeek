#include "ST25R3916Backend.h"

#ifdef DEVICE_HAS_ST25R3916
#include <rfal_rfst25r3916.h>
#include <rfal_nfc.h>
#include <st_errno.h>
#endif

ST25R3916Backend::~ST25R3916Backend() {
  end();
}

bool ST25R3916Backend::begin(SPIClass* spi, int csPin, int irqPin, uint32_t spiSpeed) {
  end();

#ifndef DEVICE_HAS_ST25R3916
  (void)spi;
  (void)csPin;
  (void)irqPin;
  (void)spiSpeed;
  _lastError = -1;
  return false;
#else
  if (!spi || csPin < 0 || irqPin < 0) {
    _lastError = ST_ERR_PARAM;
    return false;
  }

  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);
  pinMode(irqPin, INPUT);

  _rf = new RfalRfST25R3916Class(spi, csPin, irqPin, spiSpeed);
  if (!_rf) {
    _lastError = ST_ERR_NOMEM;
    return false;
  }

  _nfc = new RfalNfcClass(_rf);
  if (!_nfc) {
    _lastError = ST_ERR_NOMEM;
    end();
    return false;
  }

  ReturnCode err = _nfc->rfalNfcInitialize();
  _lastError = (int)err;
  if (err != ST_ERR_NONE) {
    end();
    _lastError = (int)err;
    return false;
  }

  _rf->st25r3916OscOn();
  _ready = true;
  return true;
#endif
}

void ST25R3916Backend::end() {
#ifdef DEVICE_HAS_ST25R3916
  if (_rf && _ready) {
    _rf->rfalFieldOff();
    _rf->rfalDeinitialize();
  }

  delete _nfc;
  _nfc = nullptr;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
  delete _rf;
#pragma GCC diagnostic pop
  _rf = nullptr;
#endif

  _ready = false;
}
