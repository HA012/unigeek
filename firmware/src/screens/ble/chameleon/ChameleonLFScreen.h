#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonLFScreen : public BaseScreen {
public:
  enum Operation { READ_TAG, LOAD_TO_SLOT, WRITE_T5577 };
  explicit ChameleonLFScreen(Operation operation = READ_TAG) : _operation(operation) {}
  const char* title() override { return _state == STATE_RESULT ? "Tag Details" : "EM410X"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };

  State _state     = STATE_IDLE;
  bool  _scanning  = false;
  bool  _needsDraw = true;
  Operation _operation = READ_TAG;

  uint8_t _uid[5] = {};

  static constexpr int kMaxRows = 10;
  ScrollListView      _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String              _rowLabels[kMaxRows];
  String              _rowValues[kMaxRows];
  uint8_t             _rowCount = 0;

  void _doScan();
  void _doLoadSlot();
  void _doT5577();
  void _saveToFile();
  void _showActions();
};
