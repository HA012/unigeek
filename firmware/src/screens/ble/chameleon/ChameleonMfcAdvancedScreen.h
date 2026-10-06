#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonMfcAdvancedScreen : public ListScreen {
public:
  const char* title() override { return "Advanced"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  ListItem _items[3];
  uint8_t _selMenu = 0;
  void _readMemory();
  void _editMemory();
  void _lockUidGen3();
};
