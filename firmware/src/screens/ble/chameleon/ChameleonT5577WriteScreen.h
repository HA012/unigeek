#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/rfid/LFCodec.h"

class ChameleonT5577WriteScreen : public ListScreen {
public:
  ChameleonT5577WriteScreen() = default;
  ChameleonT5577WriteScreen(uint8_t directSlot, uint16_t directType)
      : _directSlot(directSlot), _directType(directType), _directWrite(true) {}
  const char* title() override { return "Write to Tag"; }
  bool inhibitPowerOff() override { return _busy; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[2];
  BrowseFileView _browser;
  uint8_t _directSlot = 0;
  uint16_t _directType = 0;
  bool _directWrite = false;
  bool _preview = false;
  bool _placePrompt = false;
  bool _busy = false;

  uint16_t _sourceType = 0;
  uint8_t _sourceData[LFCodec::kMaxDataSize] = {};
  uint8_t _sourceLen = 0;
  String _sourceLabel;

  static constexpr uint8_t kMaxRows = 10;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _labels[kMaxRows];
  String _values[kMaxRows];
  uint8_t _rowCount = 0;

  void _fromFile();
  void _fromSlot();
  void _showTagPrompt();
  void _showWritingPrompt();
  void _showTryingPasswordsPrompt();
  void _performWrite();
  bool _loadFile(const String& path);
  bool _loadSlot(uint8_t slot, uint16_t type);
  void _buildPreview();
  void _addRow(const char* label, const String& value);
  void _writePreview();
  bool _writeData(uint16_t type, const uint8_t* data, uint8_t len,
                  const uint8_t* currentKey = nullptr, uint8_t keyCount = 0);
  bool _writeWithPasswordFallback(uint16_t type, const uint8_t* data, uint8_t len);
  bool _retryWithKey(uint16_t type, const uint8_t* data, uint8_t len, const uint8_t key[4]);
  bool _retryWithDictionary(uint16_t type, const uint8_t* data, uint8_t len, const char* path);
  bool _retryWithBuiltIn(uint16_t type, const uint8_t* data, uint8_t len);
  bool _passwordFallback(uint16_t type, const uint8_t* data, uint8_t len);
};
