#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonLFProtocolMenuScreen : public ListScreen {
public:
  enum Protocol { EM410X, HID_PROX, VIKING };
  explicit ChameleonLFProtocolMenuScreen(Protocol protocol) : _protocol(protocol) {}
  const char* title() override;
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  Protocol _protocol;
  ListItem _items[3];
};
