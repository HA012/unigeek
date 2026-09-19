#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/rfid/LFCodec.h"

class LfDataGeneratorScreen : public ListScreen {
public:
  const char* title() override { return "New Data"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
private:
  static constexpr uint8_t kMaxFormats = 12;
  ListItem _items[kMaxFormats];
  LFCodec::Protocol _protocols[kMaxFormats] = {};
  uint8_t _count = 0;
};
