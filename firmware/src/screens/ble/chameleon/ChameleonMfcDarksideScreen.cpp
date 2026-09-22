#include "ChameleonMfcDarksideScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/MfcKeyStore.h"
#include <cstring>

static const uint8_t kCommonKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97},
  {0xA0,0xB0,0xC0,0xD0,0xE0,0xF0},
  {0xA1,0xB1,0xC1,0xD1,0xE1,0xF1},
  {0x01,0x01,0x01,0x01,0x01,0x01},
  {0x01,0x02,0x03,0x04,0x05,0x06},
  {0x11,0x22,0x33,0x44,0x55,0x66},
  {0x00,0x11,0x22,0x33,0x44,0x55},
  {0xFC,0x00,0x01,0x8C,0x99,0x7B},
  {0xA0,0x47,0x8C,0xC3,0x90,0x91},
  {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
  {0x8F,0xD0,0xA4,0xF2,0x56,0xE9},
};
static constexpr int kCommonKeyCount =
    (int)(sizeof(kCommonKeys) / sizeof(kCommonKeys[0]));

static ChameleonMfcDarksideScreen::RecoveredKeyInfo s_lastRecovered;

ChameleonMfcDarksideScreen::RecoveredKeyInfo
ChameleonMfcDarksideScreen::takeRecoveredKey() {
  RecoveredKeyInfo out = s_lastRecovered;
  s_lastRecovered = RecoveredKeyInfo{};
  return out;
}

ChameleonMfcDarksideScreen::ChameleonMfcDarksideScreen(
    const uint8_t* uid, uint8_t uidLen, uint8_t sectors,
    const bool* foundA, const bool* foundB) {
  if (uid && uidLen > 0) {
    _uidLen = uidLen > sizeof(_uid) ? sizeof(_uid) : uidLen;
    memcpy(_uid, uid, _uidLen);
    _hasReadContext = true;
  }
  _sectors = sectors && sectors <= 40 ? sectors : 16;
  if (foundA) memcpy(_foundA, foundA, _sectors * sizeof(bool));
  if (foundB) memcpy(_foundB, foundB, _sectors * sizeof(bool));
}

void ChameleonMfcDarksideScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= 8) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount]};
  ++_rowCount;
}

uint8_t ChameleonMfcDarksideScreen::_trailerBlock(uint8_t sector) const {
  return sector < 32 ? (uint8_t)(sector * 4u + 3u)
                     : (uint8_t)(128u + (sector - 32u) * 16u + 15u);
}

bool ChameleonMfcDarksideScreen::_isMissing(uint8_t sector, bool keyB) const {
  if (sector >= _sectors || sector >= 40) return false;
  if (!_hasReadContext) return true;  // standalone: treat all as unknown
  return keyB ? !_foundB[sector] : !_foundA[sector];
}

bool ChameleonMfcDarksideScreen::_selectFirstMissing() {
  for (uint8_t s = 0; s < _sectors; ++s) {
    if (_isMissing(s, false)) {
      _target.sector = s;
      _target.block  = _trailerBlock(s);
      _target.keyB   = false;
      return _hasTarget = true;
    }
    if (_isMissing(s, true)) {
      _target.sector = s;
      _target.block  = _trailerBlock(s);
      _target.keyB   = true;
      return _hasTarget = true;
    }
  }
  _hasTarget = false;
  return false;
}

bool ChameleonMfcDarksideScreen::_selectNextMissing(int direction) {
  if (!_hasTarget || _sectors == 0) return _selectFirstMissing();
  const int total = (int)_sectors * 2;
  int current = (int)_target.sector * 2 + (_target.keyB ? 1 : 0);
  for (int step = 1; step <= total; ++step) {
    int idx = (current + direction * step) % total;
    if (idx < 0) idx += total;
    uint8_t sector = (uint8_t)(idx / 2);
    bool keyB = (idx & 1) != 0;
    if (_isMissing(sector, keyB)) {
      _target.sector = sector;
      _target.block  = _trailerBlock(sector);
      _target.keyB   = keyB;
      _hasTarget = true;
      return true;
    }
  }
  return false;
}

bool ChameleonMfcDarksideScreen::_validateTarget() const {
  return _hasTarget && _target.sector < _sectors &&
         _target.block == _trailerBlock(_target.sector) &&
         _isMissing(_target.sector, _target.keyB);
}

const char* ChameleonMfcDarksideScreen::_stateText() const {
  switch (_state) {
    case READY:   return "Ready";
    case RUNNING: return "Running";
    case SUCCESS: return "Key found";
    case FAILED:  return "No key";
    case ERROR:   return "Error";
  }
  return "Unknown";
}

const char* ChameleonMfcDarksideScreen::_statusName(uint8_t st) {
  switch (st) {
    case ChameleonClient::DARKSIDE_OK:            return "OK";
    case ChameleonClient::DARKSIDE_CANT_FIX_NT:   return "Can't fix NT";
    case ChameleonClient::DARKSIDE_LUCKY_AUTH_OK: return "Lucky auth";
    case ChameleonClient::DARKSIDE_NO_NAK_SENT:   return "No NAK";
    case ChameleonClient::DARKSIDE_TAG_CHANGED:   return "Tag changed";
  }
  return "Unknown";
}

bool ChameleonMfcDarksideScreen::_ensureTagContext() {
  if (_hasReadContext && _uidLen) return true;

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restore = c.getMode(&previousMode);
  if (!c.setMode(1)) return false;

  uint8_t atqa[2] = {}, sak = 0;
  const bool ok = c.scan14A(_uid, &_uidLen, atqa, &sak);
  if (restore) c.setMode(previousMode);
  if (!ok || !_uidLen) return false;

  _hasReadContext = true;
  if (sak == 0x18) _sectors = 40;
  else if (sak == 0x09) _sectors = 5;
  else if (!_sectors || _sectors > 40) _sectors = 16;
  return true;
}

bool ChameleonMfcDarksideScreen::_tryCommonKeys(ChameleonClient& c,
                                                uint8_t keyType,
                                                uint8_t block) {
  uint8_t found[6] = {};
  if (c.mf1CheckKeysOfBlock(block, keyType,
                            &kCommonKeys[0][0],
                            (uint8_t)kCommonKeyCount, found)) {
    memcpy(_recoveredKey, found, 6);
    _keyFound = true;
    return true;
  }
  for (int i = 0; i < kCommonKeyCount; ++i) {
    if (c.mf1CheckKey(block, keyType, kCommonKeys[i])) {
      memcpy(_recoveredKey, kCommonKeys[i], 6);
      _keyFound = true;
      return true;
    }
  }
  return false;
}

void ChameleonMfcDarksideScreen::_saveKey() {
  if (!_keyFound) return;

  if (_target.sector < 40) {
    if (_target.keyB) _foundB[_target.sector] = true;
    else              _foundA[_target.sector] = true;
  }

  s_lastRecovered.valid  = true;
  s_lastRecovered.uidLen = _uidLen;
  memcpy(s_lastRecovered.uid, _uid, _uidLen);
  s_lastRecovered.sector = _target.sector;
  s_lastRecovered.keyB   = _target.keyB;
  memcpy(s_lastRecovered.key, _recoveredKey, 6);

  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
  Uni.Storage->makeDir("/unigeek/nfc/keys");

  char uidHex[16] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidHex); i++) {
    char h[4];
    snprintf(h, sizeof(h), "%02X", _uid[i]);
    strcat(uidHex, h);
  }
  char line[48];
  snprintf(line, sizeof(line), "S%02d %c %02X%02X%02X%02X%02X%02X\n",
           _target.sector, _target.keyB ? 'B' : 'A',
           _recoveredKey[0], _recoveredKey[1], _recoveredKey[2],
           _recoveredKey[3], _recoveredKey[4], _recoveredKey[5]);
  String path = String("/unigeek/nfc/keys/") + uidHex + ".txt";
  String existing = Uni.Storage->readFile(path.c_str());
  Uni.Storage->writeFile(path.c_str(), (existing + line).c_str());
  MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, line);
}

ChameleonMfcDarksideScreen::AttackResult
ChameleonMfcDarksideScreen::_attackCurrentTarget(ChameleonClient& c) {
  const uint8_t keyType = _target.keyB ? 0x61 : 0x60;
  const uint8_t block   = _target.block;

  ChameleonClient::DarksideSample sample;
  const uint8_t kMaxAttempts = 3;
  const uint8_t kSyncMax = 8;
  bool acquired = false;

  for (uint8_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    _statusText = String("S") + String(_target.sector)
                + (_target.keyB ? "B" : "A")
                + " acq " + String(attempt + 1) + "/" + String(kMaxAttempts);
    _buildPreview();
    render();

    if (!c.mf1DarksideAcquire(keyType, block, attempt == 0, kSyncMax, &sample)) {
      _statusText = "Acquire failed";
      return ATTACK_TAG_ABORT;
    }

    if (sample.status == ChameleonClient::DARKSIDE_OK ||
        sample.status == ChameleonClient::DARKSIDE_LUCKY_AUTH_OK) {
      acquired = true;
      break;
    }
    if (sample.status == ChameleonClient::DARKSIDE_CANT_FIX_NT ||
        sample.status == ChameleonClient::DARKSIDE_NO_NAK_SENT ||
        sample.status == ChameleonClient::DARKSIDE_TAG_CHANGED) {
      _statusText = _statusName(sample.status);
      return ATTACK_TAG_ABORT;
    }
  }

  if (!acquired) {
    _statusText = String("S") + String(_target.sector)
                + (_target.keyB ? "B" : "A") + " no acquire";
    return ATTACK_NO_KEY;
  }

  _keyFound = false;
  if (_tryCommonKeys(c, keyType, block)) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
             _recoveredKey[0], _recoveredKey[1], _recoveredKey[2],
             _recoveredKey[3], _recoveredKey[4], _recoveredKey[5]);
    _keyHex = buf;
    _statusText = String("S") + String(_target.sector)
                + (_target.keyB ? "B" : "A") + " matched";
    _saveKey();
    return ATTACK_KEY_FOUND;
  }

  _statusText = String("S") + String(_target.sector)
              + (_target.keyB ? "B" : "A") + " no default";
  return ATTACK_NO_KEY;
}

bool ChameleonMfcDarksideScreen::_runSweep() {
  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restore = c.getMode(&previousMode);
  auto finish = [&](bool ok) {
    if (restore) c.setMode(previousMode);
    return ok;
  };

  if (!c.setMode(1)) {
    _statusText = "Unable to enter reader mode";
    return finish(false);
  }

  uint8_t ntLevel = 0;
  if (!c.mf1NTLevel(&ntLevel)) {
    _statusText = "Unable to read PRNG type";
    return finish(false);
  }
  if (ntLevel != 2) {
    if (ntLevel == 1) _statusText = "Use Static Nested";
    else if (ntLevel == 3) _statusText = "Hard PRNG";
    else _statusText = "Unknown PRNG";
    return finish(false);
  }

  if (!_hasTarget && !_selectFirstMissing()) {
    _statusText = "No missing key";
    return finish(false);
  }

  const int startIdx = (int)_target.sector * 2 + (_target.keyB ? 1 : 0);
  int found = 0;
  int tried = 0;
  const int maxTries = (int)_sectors * 2;

  for (int n = 0; n < maxTries; ++n) {
    if (!_validateTarget()) {
      if (!_selectNextMissing(1)) break;
      continue;
    }

    ++tried;
    const AttackResult r = _attackCurrentTarget(c);
    if (r == ATTACK_KEY_FOUND) ++found;
    else if (r == ATTACK_TAG_ABORT) return finish(found > 0);

    const int cur = (int)_target.sector * 2 + (_target.keyB ? 1 : 0);
    if (!_selectNextMissing(1)) break;
    const int nxt = (int)_target.sector * 2 + (_target.keyB ? 1 : 0);
    if (nxt == startIdx) break;
    (void)cur;
  }

  if (found > 0) {
    _statusText = String("Found ") + String(found) + " key(s)";
    _keyFound = true;
    return finish(true);
  }
  if (!_statusText.length()) _statusText = "No default key";
  return finish(false);
}

void ChameleonMfcDarksideScreen::_buildPreview() {
  _rowCount = 0;
  _addRow("Source", _hasReadContext ? "Current tag" : "Scan first");

  if (_uidLen) {
    String uid;
    char b[4];
    for (uint8_t i = 0; i < _uidLen; ++i) {
      if (i) uid += ":";
      snprintf(b, sizeof(b), "%02X", _uid[i]);
      uid += b;
    }
    _addRow("UID", uid);
  }
  if (_sectors) _addRow("Sectors", String(_sectors));

  _addRow("State", _stateText());
  _addRow("Status", _statusText.length() ? _statusText : "Idle");
  if (_keyFound) _addRow("Key", _keyHex);
  else _addRow("[Press]", _state == READY ? "Start" : "Back");
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcDarksideScreen::onInit() {
  _statusText = "Init...";
  _keyFound = false;
  _buildPreview();
  render();

  if (!_hasReadContext) {
    if (!_ensureTagContext()) {
      _state = ERROR;
      _statusText = "No tag present";
      _buildPreview();
      return;
    }
  }
  _selectFirstMissing();
  _state = _hasTarget ? READY : ERROR;
  if (!_hasTarget) _statusText = "No missing key";
  else _statusText = "Idle";
  _buildPreview();
}

void ChameleonMfcDarksideScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  const auto d = Uni.Nav->readDirection();
  if (d == INavigation::DIR_BACK) {
    Screen.goBack();
    return;
  }
  if (_busy) return;

  if (d == INavigation::DIR_LEFT || d == INavigation::DIR_RIGHT) {
    if (_selectNextMissing(d == INavigation::DIR_RIGHT ? 1 : -1)) {
      _state = READY;
      _keyFound = false;
      _statusText = "";
    }
    _buildPreview();
    render();
    return;
  }

  if (d == INavigation::DIR_PRESS) {
    if (_state == SUCCESS || _state == FAILED || _state == ERROR) {
      Screen.goBack();
      return;
    }
    if (!_validateTarget()) {
      _state = ERROR;
      ShowStatusAction::show("Invalid Darkside target", 1600);
      _buildPreview();
      render();
      return;
    }
    _busy = true;
    _state = RUNNING;
    _statusText = "Running...";
    _buildPreview();
    render();

    const bool ok = _runSweep();
    _busy = false;
    _state = ok ? SUCCESS : FAILED;
    ShowStatusAction::show(ok ? "Darkside: key found" : _statusText.c_str(), 1800);
    _buildPreview();
    render();
    return;
  }
  _scroll.onNav(d);
}

void ChameleonMfcDarksideScreen::onRender() {
  _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
}
