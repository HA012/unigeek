#include "ChameleonT5577WriteScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/rfid/T5577Dictionary.h"

static bool t5577FileType(const String& path, uint16_t* type, uint8_t* size) {
  if (!type || !size) return false;
  const int slash = path.lastIndexOf('/');
  const String name = slash >= 0 ? path.substring(slash + 1) : path;
  if (name.startsWith("EM410X_"))      { *type = 100; *size = 5;  return true; }
  if (name.startsWith("HID-Prox_"))    { *type = 200; *size = 13; return true; }
  if (name.startsWith("ioProx_"))      { *type = 201; *size = 16; return true; }
  if (name.startsWith("Viking_"))      { *type = 170; *size = 4;  return true; }
  if (name.startsWith("PAC-Stanley_")) { *type = 150; *size = 8;  return true; }
  if (name.startsWith("Jablotron_"))   { *type = 180; *size = 5;  return true; }
  return false;
}

static bool supportedT5577Type(uint16_t type) {
  return type == 100 || type == 150 || type == 170 || type == 180 || type == 200 || type == 201;
}

void ChameleonT5577WriteScreen::onInit() {
  if (_directWrite) {
    const bool ok = _writeSlot(_directSlot, _directType);
    render(); ShowStatusAction::show(ok ? "Tag written" : "Failed", 1600); render();
    Screen.goBack();
    return;
  }
  _items[0] = {"From File"};
  _items[1] = {"From Slot"};
  setItems(_items);
}

void ChameleonT5577WriteScreen::onItemSelected(uint8_t index) {
  if (index == 0) _fromFile();
  else if (index == 1) _fromSlot();
}

void ChameleonT5577WriteScreen::onBack() { Screen.goBack(); }

bool ChameleonT5577WriteScreen::_writeData(uint16_t type, const uint8_t* data, uint8_t len,
                                                const uint8_t* currentKey, uint8_t keyCount) {
  if (!data) return false;
  auto& c = ChameleonClient::get();
  if (!c.setMode(1)) return false;
  if (type == 100 && len == 5) return c.writeEM410XToT5577(data, nullptr, currentKey, keyCount);
  if (type == 200 && len == 13) return c.writeHIDProxToT5577(data, len, nullptr, currentKey, keyCount);
  if (type == 201 && len == 16) return c.writeIoProxToT5577(data, nullptr, currentKey, keyCount);
  if (type == 170 && len == 4) return c.writeVikingToT5577(data, nullptr, currentKey, keyCount);
  if (type == 150 && len == 8) return c.writePACToT5577(data, nullptr, currentKey, keyCount);
  if (type == 180 && len == 5) return c.writeJablotronToT5577(data, nullptr, currentKey, keyCount);
  return false;
}

bool ChameleonT5577WriteScreen::_retryWithKey(uint16_t type, const uint8_t* data, uint8_t len,
                                               const uint8_t key[4]) {
  _showWritingPrompt();
  return _writeData(type, data, len, key, 1);
}

bool ChameleonT5577WriteScreen::_retryWithBuiltIn(uint16_t type, const uint8_t* data, uint8_t len) {
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
  const char* choice = InputSelectAction::popup("Current Password", opts, 2 + files, nullptr);
  if (!choice) return false;
  if (!strcmp(choice, "manual")) {
    String value = InputTextAction::popup("Current Password (8 hex)", "", InputTextAction::INPUT_HEX);
    if (InputTextAction::wasCancelled()) return false;
    uint8_t key[4];
    if (!T5577Dictionary::parseKey(value, key)) {
      render(); ShowStatusAction::show("Invalid password", 1400); render(); return false;
    }
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

bool ChameleonT5577WriteScreen::_writeFile(const String& path) {
  uint16_t type = 0;
  uint8_t expected = 0;
  if (!t5577FileType(path, &type, &expected) || !Uni.Storage) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "r");
  if (!f || f.size() != expected) { if (f) f.close(); return false; }
  uint8_t data[16] = {};
  const int n = f.read(data, expected);
  f.close();
  return n == expected && _writeWithPasswordFallback(type, data, expected);
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
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("LF Data", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count) { render(); return; }

  const bool ok = _writeFile(_browser.entry(idx).path);
  render(); ShowStatusAction::show(ok ? "Tag written" : "Failed", 1600); render();
}

bool ChameleonT5577WriteScreen::_writeSlot(uint8_t slot, uint16_t type) {
  auto& c = ChameleonClient::get();
  uint8_t previousSlot = 0;
  const bool restoreSlot = c.getActiveSlot(&previousSlot) && previousSlot != slot;
  if (!c.setActiveSlot(slot)) return false;

  uint8_t data[16] = {};
  uint8_t len = 0;
  bool ok = false;
  if (type == 100) { len = 5; ok = c.getEM410XSlot(data); }
  else if (type == 200) ok = c.getHIDProxSlot(data, &len) && len == 13;
  else if (type == 201) ok = c.getIoProxSlot(data, &len) && len == 16;
  else if (type == 170) ok = c.getVikingSlot(data, &len) && len == 4;
  else if (type == 150) ok = c.getPACSlot(data, &len) && len == 8;
  else if (type == 180) ok = c.getJablotronSlot(data, &len) && len == 5;
  if (ok) ok = _writeWithPasswordFallback(type, data, len);
  if (restoreSlot) c.setActiveSlot(previousSlot);
  return ok;
}

void ChameleonT5577WriteScreen::_fromSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) { render(); ShowStatusAction::show("Failed", 1600); render(); return; }

  InputSelectAction::Option opts[8];
  String labels[8], vals[8];
  uint8_t slots[8] = {}, count = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    if (!supportedT5577Type(types[i].lfType)) continue;
    labels[count] = String("Slot ") + String(i + 1) + " - " + ChameleonClient::tagTypeName(types[i].lfType);
    vals[count] = String(count);
    slots[count] = i;
    opts[count] = {labels[count].c_str(), vals[count].c_str()};
    ++count;
  }
  if (!count) { render(); ShowStatusAction::show("No supported LF slot", 1600); render(); return; }
  const char* r = InputSelectAction::popup("From Slot", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t picked = (uint8_t)atoi(r);
  if (picked >= count) { render(); return; }
  const uint8_t slot = slots[picked];
  // Restore the parent screen immediately after dismissing the slot picker.
  // _writeSlot() performs BLE reads before the writing prompt is shown, so
  // leaving the popup pixels on screen here causes a visible transition artifact.
  render();
  const bool ok = _writeSlot(slot, types[slot].lfType);
  render(); ShowStatusAction::show(ok ? "Tag written" : "Failed", 1600); render();
}
