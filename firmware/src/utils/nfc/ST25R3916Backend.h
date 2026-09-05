#pragma once

#include <Arduino.h>
#include <SPI.h>

class RfalRfST25R3916Class;
class RfalNfcClass;

class ST25R3916Backend
{
public:
  ST25R3916Backend() = default;
  ~ST25R3916Backend();

  bool begin(SPIClass* spi, int csPin, int irqPin, uint32_t spiSpeed = 10000000);
  void end();

  bool isReady() const { return _ready; }
  int lastError() const { return _lastError; }

private:
  RfalRfST25R3916Class* _rf = nullptr;
  RfalNfcClass* _nfc = nullptr;
  bool _ready = false;
  int _lastError = 0;
};
