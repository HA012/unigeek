#pragma once
#include "ui/templates/BaseScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ui/views/ScrollListView.h"

class ChameleonMfuPagesScreen : public BaseScreen {
public:
  ChameleonMfuPagesScreen() = default;
  ChameleonMfuPagesScreen(const ChameleonClient::MfuTagInfo& info, const uint8_t* dump, uint16_t dumpLen)
    : _info(info), _dump(const_cast<uint8_t*>(dump)), _dumpLen(dumpLen),
      _ownsDump(false), _viewOnly(true) {}

  const char* title() override { return _viewOnly ? "Memory Dump" : "Read Memory"; }
  bool inhibitPowerOff() override { return _busy; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  bool _busy = false;
  bool _ready = false;
  ChameleonClient::MfuTagInfo _info = {};
  uint8_t* _dump = nullptr;
  uint16_t _dumpLen = 0;
  bool _ownsDump = true;
  bool _viewOnly = false;
  static constexpr uint16_t MAX_ROWS = 260;
  ScrollListView _view;
  ScrollListView::Row _rows[MAX_ROWS];
  String _labels[MAX_ROWS];
  uint16_t _rowCount = 0;

  void _addRow(const String& label, const String& value);
  void _read();
  void _buildRows();
  void _freeDump();
};
