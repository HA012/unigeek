#include "ChameleonT5577WriteScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"

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
  _items[0] = {"From File"};
  _items[1] = {"From Slot"};
  setItems(_items);
}

void ChameleonT5577WriteScreen::onItemSelected(uint8_t index) {
  if (index == 0) _fromFile();
  else if (index == 1) _fromSlot();
}

void ChameleonT5577WriteScreen::onBack() { Screen.goBack(); }

bool ChameleonT5577WriteScreen::_writeData(uint16_t type, const uint8_t* data, uint8_t len) {
  if (!data) return false;
  auto& c = ChameleonClient::get();
  if (type == 100 && len == 5) return c.writeEM410XToT5577(data, nullptr, nullptr, 0);
  if (type == 200 && len == 13) return c.writeHIDProxToT5577(data, len, nullptr, nullptr, 0);
  if (type == 201 && len == 16) return c.writeIoProxToT5577(data, nullptr, nullptr, 0);
  if (type == 170 && len == 4) return c.writeVikingToT5577(data, nullptr, nullptr, 0);
  if (type == 150 && len == 8) return c.writePACToT5577(data, nullptr, nullptr, 0);
  if (type == 180 && len == 5) return c.writeJablotronToT5577(data, nullptr, nullptr, 0);
  return false;
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
  return n == expected && _writeData(type, data, expected);
}

void ChameleonT5577WriteScreen::_fromFile() {
  const uint8_t n = _browser.load(this, "/unigeek/rfid",
      BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin"));
  if (!n) { render(); ShowStatusAction::show("No .bin in rfid", 1600); render(); return; }

  static constexpr uint8_t kMax = 10;
  const uint8_t count = n < kMax ? n : kMax;
  InputSelectAction::Option opts[kMax];
  String vals[kMax];
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
  if (ok) ok = _writeData(type, data, len);
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
  const bool ok = _writeSlot(slot, types[slot].lfType);
  render(); ShowStatusAction::show(ok ? "Tag written" : "Failed", 1600); render();
}
