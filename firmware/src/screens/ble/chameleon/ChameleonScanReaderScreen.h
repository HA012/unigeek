#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonScanReaderScreen : public BaseScreen {
public:
  const char* title() override { return "Detect Reader"; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  ~ChameleonScanReaderScreen() override;
private:
  enum State { STARTING, WAITING, RESULT, ERROR } _state = STARTING;
  bool _armed = false;
  bool _restoreMode = false;
  bool _restoreSlot = false;
  bool _restoreDetection = false;
  bool _previousDetection = false;
  bool _restoreHfEnable = false;
  bool _previousHfEnable = false;
  bool _restoreHfType = false;
  uint16_t _previousHfType = 0;
  bool _initedDefault = false;
  uint8_t _previousMode = 0;
  uint8_t _previousSlot = 0;
  uint32_t _baseline = 0;
  uint32_t _lastPoll = 0;
  uint32_t _probeStartedAt = 0;
  uint8_t _probeSlot = 0;
  ScrollListView _scroll;
  ScrollListView::Row _rows[6];
  String _labels[6], _values[6];
  uint8_t _rowCount = 0;
  void _drawLoading();
  void _drawWaiting();
  void _setError(const char* msg);
  void _showRecord(uint32_t index);
  void _restore();
};
