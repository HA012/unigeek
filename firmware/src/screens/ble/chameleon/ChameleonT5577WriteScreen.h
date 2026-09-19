#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class ChameleonT5577WriteScreen : public ListScreen {
public:
  const char* title() override { return "Write to Tag"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[2];
  BrowseFileView _browser;

  void _fromFile();
  void _fromSlot();
  void _showWritingPrompt();
  bool _writeFile(const String& path);
  bool _writeSlot(uint8_t slot, uint16_t type);
  bool _writeData(uint16_t type, const uint8_t* data, uint8_t len);
};
