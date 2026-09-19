#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/rfid/LFCodec.h"

class ChameleonLFScanScreen : public BaseScreen {
public:
  const char* title() override { return "Read Tag"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };

  State _state = STATE_IDLE;
  LFCodec::Protocol _protocol = LFCodec::Protocol::Unknown;
  bool _scanning = false;
  bool _needsDraw = true;
  uint8_t _data[16] = {};
  uint8_t _dataLen = 0;

  static constexpr int kMaxRows = 8;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _rowLabels[kMaxRows];
  String _rowValues[kMaxRows];
  uint8_t _rowCount = 0;

  void _draw();
  void _doScan();
  void _buildResult();
  void _addRow(const char* label, const String& value);
  const char* _protocolName() const;
  void _showActions();
  void _loadToSlot();
  void _saveToFile();
};
