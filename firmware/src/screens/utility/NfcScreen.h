#pragma once

#include "ui/templates/ListScreen.h"

class NfcScreen : public ListScreen
{
public:
  const char* title() override { return "NFC/RFID Tools"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
private:
  ListItem _items[2] = {{"NFC"}, {"RFID"}};
};

class NfcToolsScreen : public ListScreen
{
public:
  const char* title() override { return "NFC Tools"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
private:
  ListItem _items[6] = {
    {"Saved UIDs"}, {"New UID"},
    {"Saved HF Dumps"}, {"New HF Dump"},
    {"Saved NDEF Records"}, {"New NDEF Record"},
  };
};

class RfidToolsScreen : public ListScreen
{
public:
  const char* title() override { return "RFID Tools"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
private:
  ListItem _items[4] = {
    {"Saved IDs"}, {"New ID"},
    {"Saved LF Data"}, {"New LF Data"},
  };
};
