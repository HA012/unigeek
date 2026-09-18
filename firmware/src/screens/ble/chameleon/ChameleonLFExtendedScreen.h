#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonLFExtendedScreen : public BaseScreen {
public:
  enum Protocol { IOPROX, PAC_STANLEY, JABLOTRON };
  enum Operation { READ_TAG, LOAD_TO_SLOT, WRITE_T5577 };
  ChameleonLFExtendedScreen(Protocol protocol, Operation operation = READ_TAG)
      : _protocol(protocol), _operation(operation) {}
  const char* title() override;
  bool inhibitPowerOff() override { return _scanning; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };
  Protocol _protocol;
  Operation _operation;
  State _state = STATE_IDLE;
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

  const char* _protocolName() const;
  void _buildResult();
  void _doScan();
  void _doLoadSlot();
  void _doWriteTag();
  void _saveToFile();
  void _showActions();
};
