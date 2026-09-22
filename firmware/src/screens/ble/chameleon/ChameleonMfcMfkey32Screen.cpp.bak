#include "ChameleonMfcMfkey32Screen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"


static uint32_t readBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static bool isClassicType(uint16_t t) {
  return t == 1000 || t == 1001 || t == 1002 || t == 1003;
}

void ChameleonMfcMfkey32Screen::_addRow(const char* label, const String& value) {
  if (_rowCount >= 8) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount]};
  ++_rowCount;
}

void ChameleonMfcMfkey32Screen::_showPreview() {
  _state = PREVIEW;
  _rowCount = 0;
  _addRow("Slot", String(_slot + 1));
  _addRow("Type", _type);
  _addRow("UID", _uid);
  _addRow("Stored", String(_lastCount));
  _addRow("[Press]", "Start");
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcMfkey32Screen::_showRunning() {
  _rowCount = 0;
  _addRow("Slot", String(_slot + 1));
  _addRow("Type", _type);
  _addRow("UID", _uid);
  _addRow("Captured", String(_lastCount >= _baseline ? _lastCount - _baseline : 0));
  _addRow("Status", "Running...");
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcMfkey32Screen::_showFinished() {
  _state = FINISHED;
  _rowCount = 0;
  _addRow("Slot", String(_slot + 1));
  _addRow("Type", _type);
  _addRow("UID", _uid);
  _addRow("Captured", String(_parsedCount));
  _addRow("Pair", _pairReady ? "Ready" : "Not ready");
  _addRow("Status", "Finished");
  _scroll.setRows(_rows, _rowCount);
}

bool ChameleonMfcMfkey32Screen::_consumeRecord(const uint8_t record[18]) {
  // Chameleon MF1 detection record layout:
  //   block(1), flags(1), uid(4), nt(4), nr(4), ar(4)
  // Decode the complete record once. The UI currently uses only the target
  // metadata for pairing, but nr/ar remain available at the processor boundary
  // instead of being discarded during capture.
  Mfkey32Record current;
  current.valid = true;
  current.block = record[0];
  current.flags = record[1];
  current.uid = readBe32(record + 2);
  current.nt = readBe32(record + 6);
  current.nr = readBe32(record + 10);
  current.ar = readBe32(record + 14);
  ++_parsedCount;

  if (!_firstRecord.valid) {
    _firstRecord = current;
    return false;
  }

  // A useful pair must describe the same card/authentication target but be a
  // distinct observation.  The flags byte is kept opaque here: equality is
  // sufficient and avoids duplicating protocol bit definitions in the screen.
  if (_firstRecord.uid == current.uid &&
      _firstRecord.block == current.block &&
      _firstRecord.flags == current.flags &&
      _firstRecord.nt != current.nt) {
    _secondRecord = current;
    _pairReady = true;
    return true;
  }

  // Start a fresh candidate when the authentication target changes. This
  // prevents unrelated records from being presented as a compatible pair.
  if (_firstRecord.uid != current.uid ||
      _firstRecord.block != current.block ||
      _firstRecord.flags != current.flags) {
    _firstRecord = current;
  }
  return false;
}

void ChameleonMfcMfkey32Screen::_setError(const char* msg) {
  _state = ERROR;
  _ready = false;
  _rowCount = 0;
  _addRow("Status", msg);
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcMfkey32Screen::_restore() {
  // Restoration is intentionally retryable. A failed command can mean either
  // "not applied" or "applied but response lost", so keep each restore flag
  // set until the device confirms the original value was written back. The
  // destructor then gets another chance after an earlier Back/error cleanup.
  auto& c = ChameleonClient::get();
  if (_armed) {
    if (_restoreDetection && c.mf1SetDetectEnable(_previousDetection))
      _restoreDetection = false;
    if (_restoreHfEnable && c.setSlotEnable(_slot, 2, _previousHfEnable))
      _restoreHfEnable = false;
    if (_restoreMode && c.setMode(_previousMode))
      _restoreMode = false;
    _armed = _restoreDetection || _restoreHfEnable || _restoreMode;
  }
  if (_restoreSlot && c.setActiveSlot(_previousSlot))
    _restoreSlot = false;
}

void ChameleonMfcMfkey32Screen::onInit() {
  _rowCount = 0;
  _ready = false;
  _armed = false;
  _state = PREVIEW;
  auto& c = ChameleonClient::get();

  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types) || _slot >= 8 || !isClassicType(types[_slot].hfType)) {
    _setError("Slot not supported");
    return;
  }

  // Snapshot every state this screen may change before the first mutation.
  _restoreSlot = c.getActiveSlot(&_previousSlot);
  _restoreMode = c.getMode(&_previousMode);
  _restoreDetection = c.mf1GetDetectEnable(&_previousDetection);
  bool hfEn[8] = {}, lfEn[8] = {};
  _restoreHfEnable = c.getEnabledSlots(hfEn, lfEn);
  if (!_restoreSlot || !_restoreMode || !_restoreDetection || !_restoreHfEnable) {
    _restoreSlot = false; // no mutation has occurred yet
    _setError("Unable to save device state");
    return;
  }
  _previousHfEnable = hfEn[_slot];

  const bool changeSlot = _previousSlot != _slot;
  _restoreSlot = changeSlot;
  if (changeSlot && !c.setActiveSlot(_slot)) {
    _restoreSlot = false;
    _setError("Unable to select slot");
    return;
  }

  _type = ChameleonClient::tagTypeName(types[_slot].hfType);
  _uid = "Unavailable";
  ChameleonClient::AntiCollData anti{};
  if (c.getAntiCollData(&anti) && anti.uidLen) {
    _uid = "";
    char b[3];
    for (uint8_t i = 0; i < anti.uidLen; ++i) {
      snprintf(b, sizeof(b), "%02X", anti.uid[i]);
      if (i) _uid += ":";
      _uid += b;
    }
  }

  _lastCount = 0;
  c.mf1GetDetectCount(&_lastCount); // informational in preview; Start rechecks it
  _showPreview();
  _ready = true;

  int n = Achievement.inc("chameleon_mfkey32_open");
  if (n == 1) Achievement.unlock("chameleon_mfkey32_open");
}

void ChameleonMfcMfkey32Screen::_start() {
  if (!_ready || _state != PREVIEW) return;
  auto& c = ChameleonClient::get();

  // Revalidate because the Chameleon may have changed since the preview was
  // built (for example after a reconnect or an external slot change).
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types) || _slot >= 8 || !isClassicType(types[_slot].hfType)) {
    _setError("Slot not supported");
    return;
  }

  // The preview selected _slot, but the active slot may have changed since
  // then (for example after a reconnect or an external command).  Reassert
  // the target slot before arming capture.  Keep the original snapshot from
  // onInit() so _restore() still returns to the state that existed on entry.
  uint8_t activeSlot = 0;
  if (!c.getActiveSlot(&activeSlot)) {
    _setError("Unable to verify slot");
    return;
  }
  if (activeSlot != _slot && !c.setActiveSlot(_slot)) {
    _setError("Unable to select slot");
    return;
  }

  // From this point every exit path must be able to undo mode/detection/HF.
  _armed = true;
  if (!c.mf1GetDetectCount(&_baseline) ||
      (!_previousHfEnable && !c.setSlotEnable(_slot, 2, true)) ||
      !c.mf1SetDetectEnable(true) || !c.setMode(0)) {
    _setError("MFKey32 unavailable");
    _restore();
    return;
  }

  _lastCount = _baseline;
  _nextRecord = _baseline;
  _lastPoll = 0;
  _parsedCount = 0;
  _firstRecord = Mfkey32Record{};
  _secondRecord = Mfkey32Record{};
  _pairReady = false;
  _state = RUNNING;
  _showRunning();
}

void ChameleonMfcMfkey32Screen::onUpdate() {
  if (Uni.Nav->wasPressed()) {
    const auto d = Uni.Nav->readDirection();
    if (d == INavigation::DIR_BACK) {
      _restore();
      Screen.goBack();
      return;
    }
    if (d == INavigation::DIR_PRESS) {
      if (_state == PREVIEW) {
        _start();
        return;
      }
      if (_state == FINISHED || _state == ERROR) {
        _restore();
        Screen.goBack();
        return;
      }
    }
    if (_state != RUNNING) _scroll.onNav(d);
  }

  if (_state != RUNNING || !_armed) return;
  if (millis() - _lastPoll < 500) return;
  _lastPoll = millis();

  uint32_t count = 0;
  if (!ChameleonClient::get().mf1GetDetectCount(&count)) {
    _setError("Unable to read records");
    _restore();
    return;
  }
  // Counts are monotonically increasing while the detection log is intact.
  // A lower value means the log was cleared/reset while this screen was
  // running; continuing would make the baseline/index range invalid.
  if (count < _baseline || count < _nextRecord) {
    _setError("Detection log reset");
    _restore();
    return;
  }

  // Consume every newly-created record once.  This validates that the range
  // reported by DET_COUNT is actually readable and leaves a clean integration
  // boundary for a separate processor without silently skipping records when
  // several authentications arrive between two 500 ms polls.
  while (_nextRecord < count) {
    uint8_t record[18] = {};
    if (!ChameleonClient::get().mf1GetDetectRecord(_nextRecord, record)) {
      _setError("Unable to read detection");
      _restore();
      return;
    }
    ++_nextRecord;
    if (_consumeRecord(record)) {
      _lastCount = count;
      _state = PROCESSING;
      _restore();
      _showFinished();
      return;
    }
  }

  if (count != _lastCount) {
    _lastCount = count;
    _showRunning();
  }

  // Records are parsed in RAM only.  Once two compatible observations are
  // available, capture is stopped, device state is restored, and FINISHED is
  // shown. 
  // This screen deliberately does not persist raw authentication data
  // or perform key recovery.
}

void ChameleonMfcMfkey32Screen::onRender() {
  _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
}

ChameleonMfcMfkey32Screen::~ChameleonMfcMfkey32Screen() { _restore(); }
