#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonLFScanScreen : public BaseScreen {
public:
  const char* title() override { return "Scan Tag"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };
  enum Protocol { NONE, EM410X, HID_PROX, IOPROX, VIKING, PAC_STANLEY, JABLOTRON };

  State _state = STATE_IDLE;
  Protocol _protocol = NONE;
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
  String _hexData() const;
};
