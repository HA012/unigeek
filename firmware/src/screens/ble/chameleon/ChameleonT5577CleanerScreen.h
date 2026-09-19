#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/LogView.h"

class ChameleonT5577CleanerScreen : public ListScreen {
public:
  const char* title() override { return "Password Recovery"; }
  bool inhibitPowerOff() override { return _running; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  enum State { STATE_SELECT, STATE_PROMPT, STATE_RUNNING, STATE_DONE };
  State _state = STATE_SELECT;
  bool _running = false;
  BrowseFileView _browser;
  ListItem _items[1 + BrowseFileView::kCap];
  String _pickDir;
  String _sourceLabel;
  LogView _log;
  static constexpr uint16_t MAX_KEYS = 256;
  uint8_t _keys[MAX_KEYS][4] = {};
  uint16_t _keyCount = 0;
  void _loadPicker();
  bool _loadBuiltIn();
  bool _loadFile(const char* path);
  void _drawPrompt();
  void _run(const char* sourceLabel);
};
