#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class ChameleonT5577WriteScreen : public ListScreen {
public:
  ChameleonT5577WriteScreen() = default;
  ChameleonT5577WriteScreen(uint8_t directSlot, uint16_t directType)
      : _directSlot(directSlot), _directType(directType), _directWrite(true) {}
  const char* title() override { return "Write to Tag"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[2];
  BrowseFileView _browser;
  uint8_t _directSlot = 0;
  uint16_t _directType = 0;
  bool _directWrite = false;

  void _fromFile();
  void _fromSlot();
  void _showWritingPrompt();
  bool _writeFile(const String& path);
  bool _writeSlot(uint8_t slot, uint16_t type);
  bool _writeData(uint16_t type, const uint8_t* data, uint8_t len,
                  const uint8_t* currentKey = nullptr, uint8_t keyCount = 0);
  bool _writeWithPasswordFallback(uint16_t type, const uint8_t* data, uint8_t len);
  bool _retryWithKey(uint16_t type, const uint8_t* data, uint8_t len, const uint8_t key[4]);
  bool _retryWithDictionary(uint16_t type, const uint8_t* data, uint8_t len, const char* path);
  bool _retryWithBuiltIn(uint16_t type, const uint8_t* data, uint8_t len);
  bool _passwordFallback(uint16_t type, const uint8_t* data, uint8_t len);
};
