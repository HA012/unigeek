#pragma once

#include "ui/templates/ListScreen.h"

class NfcScreen : public ListScreen
{
public:
  const char* title() override { return "NFC"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[2] = {
    {"Generate NDEF Record"},
    {"Generate NFC Tag"},
  };
};
