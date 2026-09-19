#pragma once
#include "ui/templates/ListScreen.h"
class ChameleonT5577MenuScreen : public ListScreen {
public:
  const char* title() override { return "T5577"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  ListItem _items[2];
};
