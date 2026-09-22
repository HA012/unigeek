#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

// MFKey32 orchestration for a selected MIFARE Classic emulator slot.
//
// This screen owns the preview, the reversible Chameleon state changes needed
// while detection is running, and (once a usable pair is captured) classic
// mfkey32v2 key recovery.
class ChameleonMfcMfkey32Screen : public BaseScreen {
public:
  explicit ChameleonMfcMfkey32Screen(uint8_t slot) : _slot(slot) {}
  const char* title() override { return "MFKey32"; }
  bool inhibitPowerOff() override {
    return _state == RUNNING || _state == PROCESSING;
  }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  ~ChameleonMfcMfkey32Screen() override;

private:
  enum State { PREVIEW, RUNNING, PROCESSING, FINISHED, ERROR } _state = PREVIEW;
  uint8_t _slot = 0;
  uint8_t _previousSlot = 0;
  uint8_t _previousMode = 0;
  bool _previousDetection = false;
  bool _previousHfEnable = false;
  bool _restoreSlot = false;
  bool _restoreMode = false;
  bool _restoreDetection = false;
  bool _restoreHfEnable = false;
  bool _armed = false;
  bool _ready = false;
  uint32_t _baseline = 0;
  uint32_t _lastCount = 0;
  uint32_t _nextRecord = 0;
  uint32_t _lastPoll = 0;
  uint32_t _parsedCount = 0;

  // Complete decoded detection record.
  struct Mfkey32Record {
    bool valid = false;
    uint8_t block = 0;
    uint8_t flags = 0;
    uint32_t uid = 0;
    uint32_t nt = 0;
    uint32_t nr = 0;
    uint32_t ar = 0;
  };
  Mfkey32Record _firstRecord;
  Mfkey32Record _secondRecord;
  bool _pairReady = false;

  // Key recovery result (mfkey32v2)
  bool     _keyFound = false;
  uint64_t _recoveredKey = 0;
  String   _keyHex;

  String _type;
  String _uid;
  ScrollListView _scroll;
  ScrollListView::Row _rows[8];
  String _labels[8], _values[8];
  uint8_t _rowCount = 0;

  void _addRow(const char* label, const String& value);
  void _showPreview();
  void _showRunning();
  void _showProcessing();
  void _showFinished();
  bool _consumeRecord(const uint8_t record[18]);
  bool _recoverKey();                 // classic mfkey32v2
  void _saveKey();
  static bool _flagsKeyB(uint8_t flags) { return (flags & 0x01) != 0; }
  void _setError(const char* msg);
  void _restore();
  void _start();
};
