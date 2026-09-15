#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonProbeReaderScreen : public BaseScreen {
public:
  const char* title() override { return "Probe Reader"; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  ~ChameleonProbeReaderScreen() override;
private:
  enum State { WAITING, RESULT, ERROR } _state = WAITING;
  bool _armed = false;
  bool _restoreMode = false;
  bool _restoreSlot = false;
  bool _restoreDetection = false;
  bool _previousDetection = false;
  bool _restoreHfEnable = false;
  bool _previousHfEnable = false;
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
  void _drawWaiting();
  void _setError(const char* msg);
  void _showRecord(uint32_t index);
  void _restore();
};
