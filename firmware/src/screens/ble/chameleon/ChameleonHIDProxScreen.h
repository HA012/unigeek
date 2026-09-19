#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonHIDProxScreen : public BaseScreen {
public:
  const char* title() override { return _state == STATE_RESULT ? "Tag Details" : "HID Prox"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };
  State _state = STATE_IDLE;
  bool _scanning = false;
  bool _needsDraw = true;

  uint8_t _payload[13] = {};
  uint8_t _payloadLen = 0;

  static constexpr int kMaxRows = 8;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _rowLabels[kMaxRows];
  String _rowValues[kMaxRows];
  uint8_t _rowCount = 0;

  void _buildResult();
  void _doScan();
  void _doLoadSlot();
  void _doT5577();
  void _saveToFile();
  void _showActions();
};
