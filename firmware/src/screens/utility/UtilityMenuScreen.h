#pragma once

#include <Arduino.h>  // for DEVICE_HAS_WEBAUTHN
#include "ui/templates/ListScreen.h"

class UtilityMenuScreen : public ListScreen
{
public:
  const char* title()    override { return "Utilities"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
#ifdef DEVICE_HAS_WEBAUTHN
  ListItem _items[13] = {
    {"File Manager"},
    {"NFC Tools"},
    {"I2C Detector"},
    {"UART Terminal"},
    {"ESPNOW Chat"},
    {"QR Code"},
    {"Barcode"},
    {"TOTP Auth"},
    {"Manage WebAuthn"},
    {"Random Line Picker"},
    {"Pomodoro"},
    {"Wikipedia"},
    {"Achievements"},
  };
#else
  ListItem _items[12] = {
    {"File Manager"},
    {"NFC Tools"},
    {"I2C Detector"},
    {"UART Terminal"},
    {"ESPNOW Chat"},
    {"QR Code"},
    {"Barcode"},
    {"TOTP Auth"},
    {"Random Line Picker"},
    {"Pomodoro"},
    {"Wikipedia"},
    {"Achievements"},
  };
#endif
};
