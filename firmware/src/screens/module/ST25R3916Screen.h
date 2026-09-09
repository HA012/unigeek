#pragma once

#include "ui/templates/ListScreen.h"

class ST25R3916Screen : public ListScreen {
public:
  const char* title() override { return "ST25R3916"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[7] = {
    {"Scan All", "Auto I2C / SPI"},
    {"NFC-A", "ISO14443A"},
    {"NFC-B", "ISO14443B"},
    {"NFC-F / FeliCa", "212 kbps"},
    {"NFC-V / ISO15693"},
    {"Device Info (I2C)", "Grove / U216"},
    {"Device Info (SPI)", "Cap / shared SPI"},
  };

  void _scan(uint16_t techMask);
  void _showI2CInfo();
  void _showSPIInfo();
};
