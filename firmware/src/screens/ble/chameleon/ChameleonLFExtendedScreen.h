#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonLFExtendedScreen : public BaseScreen {
public:
  enum Protocol { IOPROX, PAC_STANLEY, JABLOTRON };
  explicit ChameleonLFExtendedScreen(Protocol protocol) : _protocol(protocol) {}
  const char* title() override;
  bool inhibitPowerOff() override { return _scanning; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };
  Protocol _protocol;
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
