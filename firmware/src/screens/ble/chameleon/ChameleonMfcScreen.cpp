#include "ChameleonMfcScreen.h"
#include "ChameleonMfcDarksideScreen.h"
#include "ChameleonMfcBackdoorScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/components/Header.h"
#include "ui/components/StatusBar.h"
#include "ui/views/ProgressView.h"
#include "ChameleonMfcWriteScreen.h"
#include "ChameleonMfcUidWriteScreen.h"
#include "utils/nfc/NdefParser.h"

#include "utils/nfc/MfcKeyStore.h"
#include "utils/nfc/MfcBackdoorSENRecovery.h"
#include "utils/nfc/MfcStaticNestedRecovery.h"
#include "utils/nfc/MfcNestedRecovery.h"
#include "utils/IdentityFile.h"
static constexpr uint8_t kMfcBuiltinKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97},
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
  {0xA0,0x47,0x8C,0xC3,0x90,0x91},
  {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
  {0x8F,0xD0,0xA4,0xF2,0x56,0xE9},
  {0x00,0x00,0x00,0x00,0x00,0x01},
  {0x11,0x22,0x33,0x44,0x55,0x66},
  {0x26,0x97,0x34,0x3B,0x00,0x00},
  {0x12,0x34,0x56,0x78,0x9A,0xBC},
  {0xBD,0x49,0x3A,0x39,0x62,0xB6},
};
static constexpr uint8_t kMfcBuiltinCount = sizeof(kMfcBuiltinKeys) / 6;

// ── Helpers ──

static bool _parseChameleonMfcKey(const String& line, uint8_t out[6]);

uint8_t ChameleonMfcScreen::_trailerBlock(uint8_t sector) {
  return (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
}

uint16_t ChameleonMfcScreen::_totalBlocks() {
  if (_sectors == 5)  return 20;
  if (_sectors == 40) return 256;
  return 64;
}

const char* ChameleonMfcScreen::title() {
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
  _callAuth();
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
  sp.setTextColor(TFT_CYAN);
  sp.drawString(self->_actionStatus, 2, barY);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", self->_actionPct);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, width - 2, barY);
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
  strncpy(_authStatus, "Place tag on reader...", sizeof(_authStatus) - 1);

  // STATE_AUTH renders the LogView, including its progress/status strip.
  // Drawing that intermediate state before the initial tag prompt can leave
  // a small remnant outside the body rectangle. Restore only the owning
  // screen chrome here; the LogView takes over once authentication starts.
  Header header;
  header.render(title());
  StatusBar::refresh();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);

  uint8_t atqa[2] = {}, sak = 0;
  if (!c.scan14A(_uid, &_uidLen, atqa, &sak)) {
    c.setMode(0);
    _running = false;
    render();
    ShowStatusAction::show("Tag not detected", 1200);
    Screen.goBack();
    return;
  }

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
    Screen.goBack();
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

  // Bootstrap manual attacks with the built-in key set. This keeps attacks
  // such as Nested/Static Nested consistent with Recover Keys while avoiding
  // the cost of large dictionaries.
  if (!_applyBulkKeyBatch(reinterpret_cast<const uint8_t*>(kMfcBuiltinKeys),
                          sizeof(kMfcBuiltinKeys) / sizeof(kMfcBuiltinKeys[0]))) {
    c.setMode(0);
    _running = false;
    render();
    ShowStatusAction::show("Failed", 1200);
    Screen.goBack();
    return;
  }

  // Initial scan continues with remaining fallback checks.
  static constexpr uint8_t kDefaultKey[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  int totalWork = _sectors * 2;
  int progress  = 0;
  ProgressView::init();
  snprintf(_authStatus, sizeof(_authStatus), "Authenticating keys (1/%u)...", (unsigned)_sectors);
  ProgressView::progress(_authStatus, 0);

  for (uint8_t s = 0; s < _sectors; s++) {
    uint8_t block = _trailerBlock(s);
    for (int kt = 0; kt < 2; kt++) {
      uint8_t keyType   = (kt == 0) ? 0x60 : 0x61;
      char    keyTypeCh = (kt == 0) ? 'A'  : 'B';
      _authPct = (progress * 100) / totalWork;

      bool hadSaved = (kt == 0) ? _foundA[s] : _foundB[s];
      snprintf(_authStatus, sizeof(_authStatus), "Authenticating keys (%u/%u)...",
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
        ok = c.mf1CheckKey(block, keyType, kDefaultKey);
        if (ok) {
          if (kt == 0) {
            memcpy(_keysA[s], kDefaultKey, 6);
            if (!_foundA[s]) { _foundA[s] = true; _recovered++; }
          } else {
            memcpy(_keysB[s], kDefaultKey, 6);
            if (!_foundB[s]) { _foundB[s] = true; _recovered++; }
          }
        }
      }

      progress++;
    }
  }

  snprintf(msg, sizeof(msg), "Authenticating keys (%d/%d)...", totalWork, totalWork);
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
    case ACTION_DICTIONARY:    _loadDictPicker(); break;
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

bool ChameleonMfcScreen::_tryBackdoorEncNested() {
  auto progress = [](const char* msg, int pct) {
    ProgressView::progress(msg, pct);
  };

  const auto result = MfcBackdoorSENRecovery::run(
      _sectors, _foundA, _foundB, _keysA, _keysB, progress);

  _senAvailable = result.acquired;

  if (!result.acquired) {
    ProgressView::progress("Backdoor not available", 100);
    return false;
  }

  if (!result.success) {
    ProgressView::progress("Backdoor acquired; no keys recovered", 100);
    return false;
  }

  memcpy(_foundA, result.foundA, sizeof(_foundA));
  memcpy(_foundB, result.foundB, sizeof(_foundB));
  memcpy(_keysA, result.keysA, sizeof(_keysA));
  memcpy(_keysB, result.keysB, sizeof(_keysB));
  _recovered += result.recovered;
  return true;
}

bool ChameleonMfcScreen::_applyBulkKeyBatch(const uint8_t* keys, uint8_t keyCount) {
  if (!keys || keyCount == 0) return true;

  uint8_t mask[10];
  memset(mask, 0xFF, sizeof(mask));
  bool anyMissing = false;
  for (uint8_t sector = 0; sector < _sectors && sector < 40; ++sector) {
    for (uint8_t kt = 0; kt < 2; ++kt) {
      const bool found = (kt == 0) ? _foundA[sector] : _foundB[sector];
      if (found) continue;
      const uint8_t bitIndex = (uint8_t)(sector * 2 + kt);
      mask[bitIndex / 8] &= (uint8_t)~(0x80u >> (bitIndex % 8));
      anyMissing = true;
    }
  }
  if (!anyMissing) return true;

  uint8_t foundMask[10] = {};
  uint8_t sectorKeys[40][2][6] = {};
  auto& c = ChameleonClient::get();
  if (!c.mf1CheckKeysOfSectors(mask, keys, keyCount, foundMask, sectorKeys)) return false;

  for (uint8_t sector = 0; sector < _sectors && sector < 40; ++sector) {
    for (uint8_t kt = 0; kt < 2; ++kt) {
      const uint8_t bitIndex = (uint8_t)(sector * 2 + kt);
      if ((foundMask[bitIndex / 8] & (uint8_t)(0x80u >> (bitIndex % 8))) == 0) continue;
      bool& slotFound = (kt == 0) ? _foundA[sector] : _foundB[sector];
      if (slotFound) continue;
      uint8_t* slotKey = (kt == 0) ? _keysA[sector] : _keysB[sector];
      memcpy(slotKey, sectorKeys[sector][kt], 6);
      slotFound = true;
      ++_recovered;
    }
  }
  return true;
}

ChameleonMfcScreen::ChainDictResult ChameleonMfcScreen::_runChainDictionary(
    const char* path, bool allowSkip, const String* skipKeys) {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !Uni.Storage->exists(path))
    return ChainDictResult::Completed;

  fs::File file = Uni.Storage->open(path, "r");
  if (!file) return ChainDictResult::Completed;
  const size_t totalBytes = file.size();
  uint8_t batch[83][6] = {};
  uint8_t batchCount = 0;

  auto flush = [&]() -> bool {
    if (batchCount == 0) return true;
    const bool ok = _applyBulkKeyBatch(&batch[0][0], batchCount);
    batchCount = 0;
    return ok;
  };

  while (file.available()) {
    String line = file.readStringUntil('\n');
    uint8_t key[6];
    if (!_parseChameleonMfcKey(line, key)) continue;

    char hex[13];
    snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X",
             key[0], key[1], key[2], key[3], key[4], key[5]);
    if (skipKeys && MfcKeyStore::containsKeyLine(*skipKeys, String(hex))) continue;

    bool builtin = false;
    for (uint8_t i = 0; i < kMfcBuiltinCount; ++i) {
      if (memcmp(key, kMfcBuiltinKeys[i], 6) == 0) { builtin = true; break; }
    }
    if (builtin) continue;

    memcpy(batch[batchCount++], key, 6);
    if (batchCount < 83) continue;

    if (!flush()) { file.close(); return ChainDictResult::Failed; }

    size_t rawPct = totalBytes ? (file.position() * 100u) / totalBytes : 0;
    if (rawPct > 99) rawPct = 99;
    const uint8_t pct = (uint8_t)rawPct;
    ProgressView::progress(allowSkip ? "Checking dictionary... [Press] Skip [Back] Cancel"
                                     : "Checking discovered keys... [Back] Cancel", pct);
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      const auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { file.close(); return ChainDictResult::Cancelled; }
      if (allowSkip && dir == INavigation::DIR_PRESS) { file.close(); return ChainDictResult::Skipped; }
    }
  }

  if (!flush()) { file.close(); return ChainDictResult::Failed; }
  file.close();
  return ChainDictResult::Completed;
}

void ChameleonMfcScreen::_callRecoverKeys() {
  _backdoorSENAttempted = false;
  _state = STATE_RECOVER;
  _running = true;
  render();

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restore = c.getMode(&previousMode);
  if (!c.setMode(1)) {
    _running = false;
    _resumeReadAfterAttack = false;
    if (restore) c.setMode(previousMode);
    ShowStatusAction::show("Failed to enter reader mode", 1600);
    _showReadPreview();
    return;
  }

  ProgressView::init();
  ProgressView::progress("Checking keys...", 0);
  if (!_applyBulkKeyBatch(&kMfcBuiltinKeys[0][0], kMfcBuiltinCount)) {
    if (restore) c.setMode(previousMode);
    _running = false;
    _resumeReadAfterAttack = false;
    ProgressView::finish();
    _showReadPreview();
    ShowStatusAction::show("Failed", 1400);
    return;
  }

  String discoveredKeys;
  if (Uni.Storage && Uni.Storage->isAvailable() &&
      Uni.Storage->exists(MfcKeyStore::kDiscoveredDictionary)) {
    discoveredKeys = Uni.Storage->readFile(MfcKeyStore::kDiscoveredDictionary);
    const auto discoveredResult = _runChainDictionary(
        MfcKeyStore::kDiscoveredDictionary, false, nullptr);
    if (discoveredResult == ChainDictResult::Cancelled ||
        discoveredResult == ChainDictResult::Failed) {
      if (restore) c.setMode(previousMode);
      _saveKeys();
      _running = false;
      _resumeReadAfterAttack = false;
      ProgressView::finish();
      _showReadPreview();
      ShowStatusAction::show(discoveredResult == ChainDictResult::Cancelled ? "Cancelled" : "Failed", 1400);
      return;
    }
  }

  const auto dictResult = _runChainDictionary(
      "/unigeek/nfc/dictionaries/community.txt", true,
      discoveredKeys.length() ? &discoveredKeys : nullptr);
  if (dictResult == ChainDictResult::Cancelled ||
      dictResult == ChainDictResult::Failed) {
    if (restore) c.setMode(previousMode);
    _saveKeys();
    _running = false;
    _resumeReadAfterAttack = false;
    ProgressView::finish();
    _showReadPreview();
    ShowStatusAction::show(dictResult == ChainDictResult::Cancelled ? "Cancelled" : "Failed", 1400);
    return;
  }

  bool hasKey = false;
  for (uint8_t s = 0; s < _sectors; ++s) {
    if (_foundA[s] || _foundB[s]) hasKey = true;
  }

  bool missingKeys = false;
  for (uint8_t s = 0; s < _sectors; ++s) {
    if (!_foundA[s] && !_foundB[s]) {
      missingKeys = true;
      break;
    }
  }

  if (missingKeys && !_backdoorSENAttempted) {
    _backdoorSENAttempted = true;
    ProgressView::progress("Backdoor Assisted SEN...", 80);
    if (_tryBackdoorEncNested()) {
      hasKey = false;
      for (uint8_t s = 0; s < _sectors; ++s) {
        if (_foundA[s] || _foundB[s]) hasKey = true;
      }
    }
  }

  bool readableEverySector = true;
  for (int s = 0; s < _sectors; ++s) {
    if (!_foundA[s] && !_foundB[s]) { readableEverySector = false; break; }
  }
  if (readableEverySector) {
    if (restore) c.setMode(previousMode);
    _saveKeys();
    _running = false;
    ProgressView::finish();
    const bool resumeRead = _resumeReadAfterAttack;
    _resumeReadAfterAttack = false;
    if (resumeRead) {
      _continueRead();
    } else {
      _showReadPreview();
      ShowStatusAction::show("Keys recovered", 1400);
    }
    return;
  }

  uint8_t ntLevel = 0;
  if (!c.mf1NTLevel(&ntLevel)) {
    if (restore) c.setMode(previousMode);
    _saveKeys();
    _running = false;
    _resumeReadAfterAttack = false;
    ProgressView::finish();
    _showReadPreview();
    ShowStatusAction::show("Failed", 1400);
    return;
  }

  if (restore) c.setMode(previousMode);
  _saveKeys();
  _running = false;

  if (!hasKey && ntLevel == 2) {
    ProgressView::progress("Darkside", 50);
    ChameleonMfcDarksideScreen darkside(_uid, _uidLen, _sectors, _foundA, _foundB);
    darkside.runUntilFirstKey();
    auto rec = ChameleonMfcDarksideScreen::takeRecoveredKey();
    if (rec.valid && rec.uidLen == _uidLen &&
        memcmp(rec.uid, _uid, _uidLen) == 0 && rec.sector < 40) {
      if (rec.keyB) {
        memcpy(_keysB[rec.sector], rec.key, 6);
        if (!_foundB[rec.sector]) { _foundB[rec.sector] = true; _recovered++; }
      } else {
        memcpy(_keysA[rec.sector], rec.key, 6);
        if (!_foundA[rec.sector]) { _foundA[rec.sector] = true; _recovered++; }
      }
      hasKey = true;
      _saveKeys();
    }
  }
  if (hasKey && ntLevel == 1) {
    ProgressView::progress("Static Nested", 100);
    delay(200);
    ProgressView::finish();
    _callStaticNested();
    return;
  }
  if (hasKey && ntLevel == 2) {
    ProgressView::progress("Nested Attack", 100);
    delay(200);
    ProgressView::finish();
    _callNestedAttack();
    return;
  }
  if (hasKey && ntLevel == 3) {
    ProgressView::finish();
    _resumeReadAfterAttack = false;
    _showReadPreview();
    ShowStatusAction::show("Hard PRNG — unsupported", 1800);
    return;
  }
  ProgressView::finish();
  _resumeReadAfterAttack = false;
  // Restore the result screen before the status overlay. ProgressView::finish()
  // does not reconstruct the screen that was underneath the progress view.
  _showReadPreview();
  if (!hasKey) {
    ShowStatusAction::show(ntLevel == 1 ? "Need 1 key for Static Nested" :
                           "No key found", 1800);
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

// ── Persisted keys (shared format with PN532 and ChameleonMfcDictScreen) ──

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

// ── Save keys (same format as ChameleonMfcDictScreen for interop) ──

void ChameleonMfcScreen::_saveKeys() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
  Uni.Storage->makeDir("/unigeek/nfc/keys");

  char uidHex[16] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidHex); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidHex, h);
  }
  String path = String("/unigeek/nfc/keys/") + uidHex + ".txt";
  String buf;
  for (uint8_t s = 0; s < _sectors; s++) {
    char line[48];
    if (_foundA[s]) {
      snprintf(line, sizeof(line), "S%02d A %02X%02X%02X%02X%02X%02X\n",
               s, _keysA[s][0], _keysA[s][1], _keysA[s][2],
               _keysA[s][3], _keysA[s][4], _keysA[s][5]);
      buf += line;
    }
    if (_foundB[s]) {
      snprintf(line, sizeof(line), "S%02d B %02X%02X%02X%02X%02X%02X\n",
               s, _keysB[s][0], _keysB[s][1], _keysB[s][2],
               _keysB[s][3], _keysB[s][4], _keysB[s][5]);
      buf += line;
    }
  }
  if (buf.length() > 0) {
    Uni.Storage->writeFile(path.c_str(), buf.c_str());
    MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, buf);
  }
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
  uint8_t n = _browser.load(this, _dictPickDir, ".txt", nullptr, BrowseFileView::STEM_CAPITALIZED,
                            _dictPickDir == _kDictDir ? "discovered.txt" : nullptr);

  uint8_t baseOffset = 0;
  if (_dictPickDir == _kDictDir) {
    _dictItems[0] = {"Built-in Keys"};
    baseOffset    = 1;
  }
  for (uint8_t i = 0; i < n; i++) _dictItems[i + baseOffset] = _browser.items()[i];
  _dictFileCount = n;
  _state = STATE_DICT_SEL;
  setItems(_dictItems, (uint8_t)(n + baseOffset));
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

bool ChameleonMfcScreen::_loadDictFile(const char* path) {
  _dictKeyCount = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String content = Uni.Storage->readFile(path);
  if (content.length() == 0) return false;
  int start = 0;
  while (start < (int)content.length() && _dictKeyCount < MAX_DICT_KEYS) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    uint8_t k[6];
    if (_parseChameleonMfcKey(line, k)) {
      memcpy(_dictKeys[_dictKeyCount], k, 6);
      _dictKeyCount++;
    }
    start = nl + 1;
  }
  return _dictKeyCount > 0;
}

void ChameleonMfcScreen::_runDictAttack() {
  _state   = STATE_DICT_RUN;
  _running = true;

  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Starting...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  int newFound  = 0;
  int totalWork = _sectors * 2;
  int progress  = 0;

  for (uint8_t s = 0; s < _sectors; s++) {
    uint8_t block = _trailerBlock(s);
    for (int kt = 0; kt < 2; kt++) {
      uint8_t keyType   = (kt == 0) ? 0x60 : 0x61;
      char    keyTypeCh = (kt == 0) ? 'A'  : 'B';

      if ((kt == 0) ? _foundA[s] : _foundB[s]) { progress++; continue; }

      _actionPct = (progress * 100) / totalWork;

      bool found = false;
      for (uint16_t k = 0; k < _dictKeyCount && !found; k++) {
        snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c %02X%02X%02X%02X%02X%02X",
                 s, keyTypeCh,
                 _dictKeys[k][0], _dictKeys[k][1], _dictKeys[k][2],
                 _dictKeys[k][3], _dictKeys[k][4], _dictKeys[k][5]);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

        bool ok = c.mf1CheckKey(block, keyType, _dictKeys[k]);

        char line[48];
        snprintf(line, sizeof(line), "S%d %c: %02X%02X%02X%02X%02X%02X",
                 s, keyTypeCh,
                 _dictKeys[k][0], _dictKeys[k][1], _dictKeys[k][2],
                 _dictKeys[k][3], _dictKeys[k][4], _dictKeys[k][5]);
        _actionLog.addLine(line, ok ? TFT_GREEN : TFT_RED);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

        if (ok) {
          if (kt == 0) { memcpy(_keysA[s], _dictKeys[k], 6); _foundA[s] = true; }
          else         { memcpy(_keysB[s], _dictKeys[k], 6); _foundB[s] = true; }
          _recovered++;
          newFound++;
          found = true;
        }
      }

      if (!found) {
        char nf[32];
        snprintf(nf, sizeof(nf), "  S%d %c: not found", s, keyTypeCh);
        _actionLog.addLine(nf, TFT_RED);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
      }

      progress++;
    }
  }

  char msg[64];
  if (newFound > 0)
    snprintf(msg, sizeof(msg), "Keys updated: %d new", newFound);
  else
    snprintf(msg, sizeof(msg), "No new keys found");
  strncpy(_actionStatus, msg, sizeof(_actionStatus) - 1);
  _actionPct = 100;
  _actionLog.addLine(msg, newFound > 0 ? TFT_GREEN : TFT_RED);
  _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

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

void ChameleonMfcScreen::_callStaticNested() {
  _state = STATE_STATIC_NESTED;
  _running = true;
  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Starting...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);
  auto log = [](const char* msg, MfcStaticNestedRecovery::LogLevel level, void* ctx) {
    auto* self = static_cast<ChameleonMfcScreen*>(ctx);
    uint16_t color = TFT_CYAN;
    if (level == MfcStaticNestedRecovery::LogLevel::Success) color = TFT_GREEN;
    else if (level == MfcStaticNestedRecovery::LogLevel::Warning) color = TFT_YELLOW;
    else if (level == MfcStaticNestedRecovery::LogLevel::Error) color = TFT_RED;
    else if (level == MfcStaticNestedRecovery::LogLevel::Debug) return;
    self->_log(msg, color);
  };
  auto progress = [](const char* msg, int pct, void* ctx) {
    auto* self = static_cast<ChameleonMfcScreen*>(ctx);
    self->_actionPct = pct;
    strncpy(self->_actionStatus, msg, sizeof(self->_actionStatus) - 1);
    self->_actionStatus[sizeof(self->_actionStatus) - 1] = '\0';
    self->_actionLog.draw(Uni.Lcd, self->bodyX(), self->bodyY(), self->bodyW(), self->bodyH(), self->_actionStatusBarCb, self);
  };
  const auto result = MfcStaticNestedRecovery::run(_sectors, _uid, _uidLen, _foundA, _foundB, _keysA, _keysB, log, progress, this);
  if (!result.started) { c.setMode(0); _running = false; _state = STATE_STATIC_NESTED_LOG; return; }
  memcpy(_foundA, result.foundA, sizeof(_foundA)); memcpy(_foundB, result.foundB, sizeof(_foundB));
  memcpy(_keysA, result.keysA, sizeof(_keysA)); memcpy(_keysB, result.keysB, sizeof(_keysB)); _recovered += result.recovered;
  char m[64]; if (result.recovered > 0) snprintf(m, sizeof(m), "Keys updated: %d new", result.recovered); else snprintf(m, sizeof(m), "No new keys found");
  strncpy(_actionStatus, m, sizeof(_actionStatus) - 1); _actionPct = 100; _log(m, result.recovered > 0 ? TFT_GREEN : TFT_YELLOW);
  if (result.recovered > 0) { _saveKeys(); int n = Achievement.inc("chameleon_static_nested"); if (n == 1) Achievement.unlock("chameleon_static_nested"); Achievement.setMax("chameleon_mfc_keys_found", _recovered); if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found"); }
  c.setMode(0); _running = false; _state = STATE_STATIC_NESTED_LOG;
}

// ── Nested Attack ─────────────────────────────────────────────────────────────

void ChameleonMfcScreen::_callNestedAttack() {
  _state = STATE_NESTED;
  _running = true;
  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Starting...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);
  auto log = [](const char* msg, MfcNestedRecovery::LogLevel level, void* ctx) {
    auto* self = static_cast<ChameleonMfcScreen*>(ctx);
    uint16_t color = TFT_CYAN;
    if (level == MfcNestedRecovery::LogLevel::Success) color = TFT_GREEN;
    else if (level == MfcNestedRecovery::LogLevel::Warning) color = TFT_YELLOW;
    else if (level == MfcNestedRecovery::LogLevel::Error) color = TFT_RED;
    else if (level == MfcNestedRecovery::LogLevel::Debug) return;
    self->_log(msg, color);
  };
  auto progress = [](const char* msg, int pct, void* ctx) {
    auto* self = static_cast<ChameleonMfcScreen*>(ctx);
    self->_actionPct = pct;
    strncpy(self->_actionStatus, msg, sizeof(self->_actionStatus) - 1);
    self->_actionStatus[sizeof(self->_actionStatus) - 1] = '\0';
    self->_actionLog.draw(Uni.Lcd, self->bodyX(), self->bodyY(), self->bodyW(), self->bodyH(), self->_actionStatusBarCb, self);
  };
  const auto result = MfcNestedRecovery::run(_sectors, _uid, _uidLen, _foundA, _foundB, _keysA, _keysB, log, progress, this);
  if (!result.started) { c.setMode(0); _running = false; _state = STATE_NESTED_LOG; return; }
  memcpy(_foundA, result.foundA, sizeof(_foundA)); memcpy(_foundB, result.foundB, sizeof(_foundB));
  memcpy(_keysA, result.keysA, sizeof(_keysA)); memcpy(_keysB, result.keysB, sizeof(_keysB)); _recovered += result.recovered;
  char m[64]; if (result.recovered > 0) snprintf(m, sizeof(m), "Keys updated: %d new", result.recovered); else snprintf(m, sizeof(m), "No new keys found");
  strncpy(_actionStatus, m, sizeof(_actionStatus) - 1); _actionPct = 100; _log(m, result.recovered > 0 ? TFT_GREEN : TFT_YELLOW);
  if (result.recovered > 0) { _saveKeys(); int n = Achievement.inc("chameleon_nested_attack"); if (n == 1) Achievement.unlock("chameleon_nested_attack"); Achievement.setMax("chameleon_mfc_keys_found", _recovered); if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found"); }
  c.setMode(0); _running = false; _state = STATE_NESTED_LOG;
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

  if (_state == STATE_DICT_LOG ||
      _state == STATE_STATIC_NESTED_LOG ||
      _state == STATE_NESTED_LOG) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
      if (dir == INavigation::DIR_PRESS) {
        if (_resumeReadAfterAttack) {
          _resumeReadAfterAttack = false;
          _continueRead();
        } else {
          Screen.goBack();
        }
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
    uint8_t baseOffset = (_dictPickDir == _kDictDir) ? 1 : 0;
    if (baseOffset && index == 0) {
      _dictKeyCount = kMfcBuiltinCount;
      memcpy(_dictKeys, kMfcBuiltinKeys, kMfcBuiltinCount * 6);
    } else {
      uint8_t fi = index - baseOffset;
      if (fi >= _browser.count()) return;
      const auto& e = _browser.entry(fi);
      if (e.isDir) {
        _dictPickDir = e.path;
        _loadDictPicker();
        return;
      }
      if (!_loadDictFile(e.path.c_str())) {
        ShowStatusAction::show("Failed to load keys", 1200);
        render();
        return;
      }
    }
    if (_dictKeyCount == 0) {
      ShowStatusAction::show("No dictionary files", 1200);
      render();
      return;
    }
    _runDictAttack();
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
