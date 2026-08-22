#include "ChameleonMfuToolsScreen.h"
#include "ChameleonMfuScreen.h"
#include "ChameleonMfuWriteScreen.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonMfuToolsScreen::onInit() {
  _items[0] = {"Read Tag"};
  _items[1] = {"Write Tag"};
  setItems(_items);
}

void ChameleonMfuToolsScreen::_writeFromFile() {
  static constexpr uint8_t kMax = 10;
  uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", ".bin");
  if (!n) {
    render();
    ShowStatusAction::show("No .bin in nfc/dumps", 1500);
    render();
    return;
  }
  const uint8_t count = n < kMax ? n : kMax;
  InputSelectAction::Option opts[kMax];
  String vals[kMax];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("NTAG215 dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count) { render(); return; }
  String path = _browser.entry(idx).path;
  render();
  Screen.push(new ChameleonMfuWriteScreen(path));
}

void ChameleonMfuToolsScreen::_writeFromSlot() {
  static const InputSelectAction::Option opts[] = {
    {"Slot 1", "0"}, {"Slot 2", "1"}, {"Slot 3", "2"}, {"Slot 4", "3"},
    {"Slot 5", "4"}, {"Slot 6", "5"}, {"Slot 7", "6"}, {"Slot 8", "7"},
  };
  const char* r = InputSelectAction::popup("Source slot", opts, 8, nullptr);
  if (!r) { render(); return; }
  Screen.push(new ChameleonMfuWriteScreen((uint8_t)atoi(r)));
}

void ChameleonMfuToolsScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"From File", "file"},
    {"From Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}

void ChameleonMfuToolsScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfuScreen());
  else if (index == 1) _writeTag();
}

void ChameleonMfuToolsScreen::onBack() { Screen.goBack(); }
