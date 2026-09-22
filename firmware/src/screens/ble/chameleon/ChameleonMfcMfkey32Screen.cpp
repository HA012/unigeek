#include "ChameleonMfcMfkey32Screen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/MfcKeyStore.h"
#include "utils/crypto/crapto1.h"

static uint32_t readBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8)  | uint32_t(p[3]);
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
  _addRow("Need", "2 different NT");
  _addRow("Status", "Running...");
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcMfkey32Screen::_showProcessing() {
  _state = PROCESSING;
  _rowCount = 0;
  _addRow("Slot", String(_slot + 1));
  _addRow("Type", _type);
  _addRow("UID", _uid);
  _addRow("Processed", String(_parsedCount));
  if (_pairReady) {
    _addRow("Block", String(_firstRecord.block));
    _addRow("KeyType", _flagsKeyB(_firstRecord.flags) ? "Key B" : "Key A");
  }
  _addRow("Status", "Processing...");
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcMfkey32Screen::_showFinished() {
  _state = FINISHED;
  _rowCount = 0;
  _addRow("Slot", String(_slot + 1));
  _addRow("Type", _type);
  _addRow("UID", _uid);
  _addRow("Processed", String(_parsedCount));
  if (_pairReady) {
    _addRow("Block", String(_firstRecord.block));
    _addRow("KeyType", _flagsKeyB(_firstRecord.flags) ? "Key B" : "Key A");
    _addRow("Key", _keyFound ? _keyHex : (_keyHex.length() ? _keyHex : "—"));
  } else {
    _addRow("Pair", "Not ready");
  }
  _addRow("Status", "Finished");
  _scroll.setRows(_rows, _rowCount);
}

bool ChameleonMfcMfkey32Screen::_consumeRecord(const uint8_t record[18]) {
  // Layout: block(1), flags(1), uid(4), nt(4), nr(4), ar(4)
  Mfkey32Record current;
  current.valid = true;
  current.block = record[0];
  current.flags = record[1];
  current.uid   = readBe32(record + 2);
  current.nt    = readBe32(record + 6);
  current.nr    = readBe32(record + 10);
  current.ar    = readBe32(record + 14);
  ++_parsedCount;

  if (!_firstRecord.valid) {
    _firstRecord = current;
    return false;
  }

  // Same target, different nonce → usable pair
  if (_firstRecord.uid   == current.uid &&
      _firstRecord.block == current.block &&
      _firstRecord.flags == current.flags &&
      _firstRecord.nt    != current.nt) {
    _secondRecord = current;
    _pairReady = true;
    return true;
  }

  // Target changed → restart candidate
  if (_firstRecord.uid   != current.uid ||
      _firstRecord.block != current.block ||
      _firstRecord.flags != current.flags) {
    _firstRecord = current;
  }
  return false;
}

bool ChameleonMfcMfkey32Screen::_recoverKey() {
  if (!_firstRecord.valid || !_secondRecord.valid) return false;

  const uint32_t uid = _firstRecord.uid;
  const uint32_t nt0 = _firstRecord.nt;
  const uint32_t nr0 = _firstRecord.nr;
  const uint32_t ar0 = _firstRecord.ar;
  const uint32_t nt1 = _secondRecord.nt;
  const uint32_t nr1 = _secondRecord.nr;
  const uint32_t ar1 = _secondRecord.ar;

  const uint32_t p64  = prng_successor(nt0, 64);
  const uint32_t p64b = prng_successor(nt1, 64);

  struct Crypto1State* s = lfsr_recovery32(ar0 ^ p64, 0);
  if (!s) return false;

  bool found = false;
  uint64_t key = 0;

  for (struct Crypto1State* t = s; t->odd | t->even; ++t) {
    lfsr_rollback_word(t, 0, 0);
    lfsr_rollback_word(t, nr0, 1);
    lfsr_rollback_word(t, uid ^ nt0, 0);
    crypto1_get_lfsr(t, &key);

    crypto1_word(t, uid ^ nt1, 0);
    crypto1_word(t, nr1, 1);
    if (ar1 == (crypto1_word(t, 0, 0) ^ p64b)) {
      found = true;
      break;
    }
  }

  crypto1_destroy(s);

  if (found) {
    _recoveredKey = key;
    char buf[16];
    snprintf(buf, sizeof(buf), "%012llX", (unsigned long long)key);
    _keyHex = buf;
    _keyFound = true;
    _saveKey();
  }
  return found;
}

void ChameleonMfcMfkey32Screen::_saveKey() {
  if (!_keyFound) return;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
  Uni.Storage->makeDir("/unigeek/nfc/keys");

  char uidHex[16] = {};
  snprintf(uidHex, sizeof(uidHex), "%08lX", (unsigned long)_firstRecord.uid);

  uint8_t kb[6] = {};
  uint64_t tmp = _recoveredKey;
  for (int i = 5; i >= 0; --i) {
    kb[i] = (uint8_t)(tmp & 0xFF);
    tmp >>= 8;
  }

  const uint8_t sector = _firstRecord.block < 128
      ? (uint8_t)(_firstRecord.block / 4)
      : (uint8_t)(32 + (_firstRecord.block - 128) / 16);

  char line[48];
  snprintf(line, sizeof(line), "S%02d %c %02X%02X%02X%02X%02X%02X\n",
           sector, _flagsKeyB(_firstRecord.flags) ? 'B' : 'A',
           kb[0], kb[1], kb[2], kb[3], kb[4], kb[5]);
  String path = String("/unigeek/nfc/keys/") + uidHex + ".txt";
  String existing = Uni.Storage->readFile(path.c_str());
  Uni.Storage->writeFile(path.c_str(), (existing + line).c_str());
  MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, line);
}

void ChameleonMfcMfkey32Screen::_setError(const char* msg) {
  _state = ERROR;
  _ready = false;
  _rowCount = 0;
  _addRow("Status", msg);
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcMfkey32Screen::_restore() {
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
  _keyFound = false;
  _keyHex = "";
  _pairReady = false;
  _firstRecord = {};
  _secondRecord = {};

  auto& c = ChameleonClient::get();

  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types) || _slot >= 8 || !isClassicType(types[_slot].hfType)) {
    _setError("Slot not supported");
    return;
  }

  _restoreSlot      = c.getActiveSlot(&_previousSlot);
  _restoreMode      = c.getMode(&_previousMode);
  _restoreDetection = c.mf1GetDetectEnable(&_previousDetection);
  bool hfEn[8] = {}, lfEn[8] = {};
  _restoreHfEnable  = c.getEnabledSlots(hfEn, lfEn);

  if (!_restoreSlot || !_restoreMode || !_restoreDetection || !_restoreHfEnable) {
    _restoreSlot = false;
    _setError("Unable to save device state");
    return;
  }
  _previousHfEnable = hfEn[_slot];

  const bool changeSlot = (_previousSlot != _slot);
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
  c.mf1GetDetectCount(&_lastCount);
  _showPreview();
  _ready = true;

  int n = Achievement.inc("chameleon_mfkey32_open");
  if (n == 1) Achievement.unlock("chameleon_mfkey32_open");
}

void ChameleonMfcMfkey32Screen::_start() {
  if (!_ready || _state != PREVIEW) return;
  auto& c = ChameleonClient::get();

  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types) || _slot >= 8 || !isClassicType(types[_slot].hfType)) {
    _setError("Slot not supported");
    return;
  }

  uint8_t activeSlot = 0;
  if (!c.getActiveSlot(&activeSlot)) {
    _setError("Unable to verify slot");
    return;
  }
  if (activeSlot != _slot && !c.setActiveSlot(_slot)) {
    _setError("Unable to select slot");
    return;
  }

  _armed = true;
  if (!c.mf1GetDetectCount(&_baseline) ||
      (!_previousHfEnable && !c.setSlotEnable(_slot, 2, true)) ||
      !c.mf1SetDetectEnable(true) ||
      !c.setMode(0)) {
    _setError("MFKey32 unavailable");
    _restore();
    return;
  }

  _lastCount   = _baseline;
  _nextRecord  = _baseline;
  _lastPoll    = 0;
  _parsedCount = 0;
  _firstRecord = {};
  _secondRecord = {};
  _pairReady   = false;
  _keyFound    = false;
  _keyHex      = "";
  _state       = RUNNING;
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

  if (_state == PROCESSING) {
    // Processing is deliberately deferred until the update after the pair was
    // captured. This gives the UI a frame to render the PROCESSING state.
    if (!_recoverKey()) {
      _keyHex = "Not found";
    }
    _showFinished();
    return;
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

  if (count < _baseline || count < _nextRecord) {
    _setError("Detection log reset");
    _restore();
    return;
  }

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
      _restore();                       // stop detection / restore device first
      _showProcessing();                // render on the next frame before work
      return;
    }
  }

  if (count != _lastCount) {
    _lastCount = count;
    _showRunning();
  }
}

void ChameleonMfcMfkey32Screen::onRender() {
  _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
}

ChameleonMfcMfkey32Screen::~ChameleonMfcMfkey32Screen() {
  _restore();
}
