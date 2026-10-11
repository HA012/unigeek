#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonHFMenuScreen : public ListScreen {
public:
  const char* title() override { return "HF Tools"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  bool _families = false;
  ListItem _items[2];
  ListItem _mainItems[3];
  uint8_t _mainSelection = 0;
  uint8_t _familySelection = 0;
};
