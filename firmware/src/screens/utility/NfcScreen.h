#pragma once

#include "ui/templates/ListScreen.h"

class NfcScreen : public ListScreen
{
public:
  const char* title() override { return "NFC/RFID Tools"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[6] = {
    {"New HF Dump"},
    {"Edit HF Dump"},
    {"New LF Data"},
    {"Edit LF Data"},
    {"New NDEF Record"},
    {"Edit NDEF Record"},
  };
};
