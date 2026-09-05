#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/nfc/ST25R3916Backend.h"

class ST25R3916Screen : public ListScreen
{
public:
  const char* title() override { return "ST25R3916"; }
  bool inhibitPowerOff() override { return true; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[1] = {
    {"Device Info"},
  };

  ST25R3916Backend _backend;
  void _showDeviceInfo();
};
