#include "ChameleonT5577WriteScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/rfid/T5577Dictionary.h"
#include "utils/rfid/LFCodec.h"

void ChameleonT5577WriteScreen::onInit() {
  if (_directWrite) {
    if (!_loadSlot(_directSlot, _directType)) {
      render(); ShowStatusAction::show("Slot unavailable", 1600); Screen.goBack(); return;
    }
    _sourceLabel = String("Slot ") + String(_directSlot + 1);
    _buildPreview();
    _preview = true;
    render();
    return;
  }
  _items[0] = {"From File"};
  _items[1] = {"From Slot"};
  setItems(_items);
}

void ChameleonT5577WriteScreen::onUpdate() {
  if (!_preview) { ListScreen::onUpdate(); return; }
  if (_busy || !Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (_placePrompt) {
    if (dir == INavigation::DIR_BACK) { _placePrompt = false; render(); return; }
    if (dir == INavigation::DIR_PRESS) { _placePrompt = false; _performWrite(); return; }
    return;
  }
  if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
  if (dir == INavigation::DIR_PRESS) { _writePreview(); return; }
  _scrollView.onNav(dir);
}

void ChameleonT5577WriteScreen::onRender() {
  if (_placePrompt) { _showTagPrompt(); return; }
  if (_preview) { _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH()); return; }
  ListScreen::onRender();
}

void ChameleonT5577WriteScreen::onItemSelected(uint8_t index) {
  if (index == 0) _fromFile();
  else if (index == 1) _fromSlot();
}

void ChameleonT5577WriteScreen::onBack() { Screen.goBack(); }

void ChameleonT5577WriteScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= kMaxRows) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount]};
  ++_rowCount;
}

void ChameleonT5577WriteScreen::_buildPreview() {
  _rowCount = 0;
  const LFCodec::FormatInfo* info = LFCodec::fromChameleonType(_sourceType);
  if (!info) return;
  LFCodec::DecodedData decoded;
  if (!LFCodec::decode(info->protocol, _sourceData, _sourceLen, decoded)) return;
  _addRow("Source", _sourceLabel);
  _addRow("Type", info->name);
  LFCodec::Field fields[3];
  const size_t fieldCount = LFCodec::fields(decoded, fields, 3);
  for (size_t i = 0; i < fieldCount; ++i) _addRow(fields[i].label, fields[i].value);
  _addRow("Frequency", "125 kHz");
  _addRow("[Press]", "Write to Tag");
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
}

bool ChameleonT5577WriteScreen::_writeData(uint16_t type, const uint8_t* data, uint8_t len,
                                                const uint8_t* currentKey, uint8_t keyCount) {
  if (!data) return false;
  const LFCodec::FormatInfo* info = LFCodec::fromChameleonType(type);
  if (!info || !LFCodec::isSupportedT5577(type) || !LFCodec::validate(info->protocol, len)) return false;
  auto& c = ChameleonClient::get();
  if (!c.setMode(1)) return false;
  switch (info->protocol) {
    case LFCodec::Protocol::EM410X:     return c.writeEM410XToT5577(data, nullptr, currentKey, keyCount);
    case LFCodec::Protocol::HIDProx:    return c.writeHIDProxToT5577(data, len, nullptr, currentKey, keyCount);
    case LFCodec::Protocol::IoProx:     return c.writeIoProxToT5577(data, nullptr, currentKey, keyCount);
    case LFCodec::Protocol::Viking:     return c.writeVikingToT5577(data, nullptr, currentKey, keyCount);
    case LFCodec::Protocol::PACStanley: return c.writePACToT5577(data, nullptr, currentKey, keyCount);
    case LFCodec::Protocol::Jablotron:  return c.writeJablotronToT5577(data, nullptr, currentKey, keyCount);
    default: return false;
  }
}

bool ChameleonT5577WriteScreen::_retryWithKey(uint16_t type, const uint8_t* data, uint8_t len,
                                               const uint8_t key[4]) {
  return _writeData(type, data, len, key, 1);
}

bool ChameleonT5577WriteScreen::_retryWithBuiltIn(uint16_t type, const uint8_t* data, uint8_t len) {
  _showTryingPasswordsPrompt();
  for (size_t i = 0; i < T5577Dictionary::kBuiltinKeyCount; ++i) {
    if (_retryWithKey(type, data, len, T5577Dictionary::kBuiltinKeys[i])) return true;
  }
  return false;
}

bool ChameleonT5577WriteScreen::_retryWithDictionary(uint16_t type, const uint8_t* data, uint8_t len,
                                                      const char* path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String content = Uni.Storage->readFile(path);
  if (!content.length()) return false;
  _showTryingPasswordsPrompt();
  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start); if (nl < 0) nl = content.length();
    String line = content.substring(start, nl); line.trim();
    if (line.length() && !line.startsWith("#")) {
      uint8_t key[4];
      if (T5577Dictionary::parseKey(line, key) && _retryWithKey(type, data, len, key)) return true;
    }
    start = nl + 1;
  }
  return false;
}

bool ChameleonT5577WriteScreen::_passwordFallback(uint16_t type, const uint8_t* data, uint8_t len) {
  // Reuse the screen-owned browser. Keeping a second BrowseFileView plus
  // popup arrays on this nested call path can exhaust the small UI task stack.
  // Confine the picker to the dictionary directory.  Besides preventing
  // navigation outside it, this suppresses the synthetic ".." entry.
  _browser.root = T5577Dictionary::kDirectory;
  const uint8_t n = _browser.load(this, T5577Dictionary::kDirectory, ".txt", nullptr, BrowseFileView::STEM_CAPITALIZED);
  static constexpr uint8_t kMaxFiles = 8;
  const uint8_t files = n < kMaxFiles ? n : kMaxFiles;
  static InputSelectAction::Option opts[2 + kMaxFiles];
  static String vals[2 + kMaxFiles];
  opts[0] = {"Enter Password", "manual"};
  opts[1] = {"Built-in Keys", "builtin"};
  for (uint8_t i = 0; i < files; ++i) {
    vals[i + 2] = String(i);
    opts[i + 2] = {_browser.entry(i).label.c_str(), vals[i + 2].c_str()};
  }
  _clearPopupBackground();
  const char* choice = InputSelectAction::popup("Current Password", opts, 2 + files, nullptr);
  if (!choice) return false;
  if (!strcmp(choice, "manual")) {
    String value = InputTextAction::popup("Current Password (8 hex)", "", InputTextAction::INPUT_HEX);
    if (InputTextAction::wasCancelled()) return false;
    uint8_t key[4];
    if (!T5577Dictionary::parseKey(value, key)) {
      render(); ShowStatusAction::show("Invalid password", 1400); render(); return false;
    }
    _showWritingPrompt();
    return _retryWithKey(type, data, len, key);
  }
  if (!strcmp(choice, "builtin")) return _retryWithBuiltIn(type, data, len);
  const uint8_t idx = (uint8_t)atoi(choice);
  if (idx >= files) return false;
  // Copy before entering the retry path; later UI operations may reuse _browser.
  const String dictPath = _browser.entry(idx).path;
  return _retryWithDictionary(type, data, len, dictPath.c_str());
}

bool ChameleonT5577WriteScreen::_writeWithPasswordFallback(uint16_t type, const uint8_t* data, uint8_t len) {
  _showWritingPrompt();
  if (_writeData(type, data, len)) return true;
  render();
  return _passwordFallback(type, data, len);
}

void ChameleonT5577WriteScreen::_showTagPrompt() {
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY();
  const int bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2 - 8);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString("[Press] Continue", bx + bw / 2, by + bh / 2 + 10);
}

void ChameleonT5577WriteScreen::_showWritingPrompt() {
  render();
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY();
  const int bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Writing tag...", bx + bw / 2, by + bh / 2);
}

void ChameleonT5577WriteScreen::_clearPopupBackground() {
  // Password popups are overlays. Clear the previous preview/details body first
  // so stale rows do not remain visible around the popup. Keep the screen
  // chrome/title intact.
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
}

void ChameleonT5577WriteScreen::_showTryingPasswordsPrompt() {
  render();
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Trying passwords...", bx + bw / 2, by + bh / 2);
}

bool ChameleonT5577WriteScreen::_loadFile(const String& path) {
  const LFCodec::FormatInfo* info = LFCodec::fromFilename(path);
  if (!info || !Uni.Storage) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "r");
  if (!f || f.size() != info->dataSize) { if (f) f.close(); return false; }
  const int n = f.read(_sourceData, info->dataSize);
  f.close();
  if (n != info->dataSize) return false;
  _sourceType = info->chameleonType;
  _sourceLen = info->dataSize;
  _sourceLabel = "File";
  return true;
}

void ChameleonT5577WriteScreen::_fromFile() {
  const uint8_t n = _browser.load(this, "/unigeek/rfid",
      BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin"));
  if (!n) { render(); ShowStatusAction::show("No .bin in rfid", 1600); render(); return; }
  static constexpr uint8_t kMax = 10;
  const uint8_t count = n < kMax ? n : kMax;
  static InputSelectAction::Option opts[kMax];
  static String vals[kMax];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i); opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("LF Data", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count || !_loadFile(_browser.entry(idx).path)) {
    render(); ShowStatusAction::show("Invalid LF data", 1600); render(); return;
  }
  _buildPreview(); _preview = true; render();
}

bool ChameleonT5577WriteScreen::_loadSlot(uint8_t slot, uint16_t type) {
  const LFCodec::FormatInfo* info = LFCodec::fromChameleonType(type);
  if (!info || !LFCodec::isSupportedT5577(type)) return false;
  auto& c = ChameleonClient::get();
  uint8_t previousSlot = 0;
  const bool restoreSlot = c.getActiveSlot(&previousSlot) && previousSlot != slot;
  if (!c.setActiveSlot(slot)) return false;
  uint8_t len = 0; bool ok = false;
  switch (info->protocol) {
    case LFCodec::Protocol::EM410X: len = 5; ok = c.getEM410XSlot(_sourceData); break;
    case LFCodec::Protocol::HIDProx: ok = c.getHIDProxSlot(_sourceData, &len); break;
    case LFCodec::Protocol::IoProx: ok = c.getIoProxSlot(_sourceData, &len); break;
    case LFCodec::Protocol::Viking: ok = c.getVikingSlot(_sourceData, &len); break;
    case LFCodec::Protocol::PACStanley: ok = c.getPACSlot(_sourceData, &len); break;
    case LFCodec::Protocol::Jablotron: ok = c.getJablotronSlot(_sourceData, &len); break;
    default: break;
  }
  if (restoreSlot) c.setActiveSlot(previousSlot);
  if (!ok || !LFCodec::validate(info->protocol, len)) return false;
  _sourceType = type; _sourceLen = len;
  return true;
}

void ChameleonT5577WriteScreen::_fromSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) { render(); ShowStatusAction::show("Failed", 1600); render(); return; }
  InputSelectAction::Option opts[8]; String labels[8], vals[8]; uint8_t slots[8] = {}, count = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    if (!LFCodec::isSupportedT5577(types[i].lfType)) continue;
    labels[count] = String("Slot ") + String(i + 1) + " - " + ChameleonClient::tagTypeName(types[i].lfType);
    vals[count] = String(count); slots[count] = i; opts[count] = {labels[count].c_str(), vals[count].c_str()}; ++count;
  }
  if (!count) { render(); ShowStatusAction::show("No supported LF slot", 1600); render(); return; }
  const char* r = InputSelectAction::popup("From Slot", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t picked = (uint8_t)atoi(r);
  if (picked >= count) { render(); return; }
  const uint8_t slot = slots[picked];
  render();
  if (!_loadSlot(slot, types[slot].lfType)) { ShowStatusAction::show("Slot unavailable", 1600); render(); return; }
  _sourceLabel = String("Slot ") + String(slot + 1);
  _buildPreview(); _preview = true; render();
}

void ChameleonT5577WriteScreen::_writePreview() {
  _placePrompt = true;
  render();
}

void ChameleonT5577WriteScreen::_performWrite() {
  _busy = true;
  const bool ok = _writeWithPasswordFallback(_sourceType, _sourceData, _sourceLen);
  _busy = false;
  _buildPreview(); render();
  if (ok) { ShowStatusAction::show("Tag written", 1600); Screen.goBack(); return; }
  ShowStatusAction::show("Failed", 1600); render();
}
