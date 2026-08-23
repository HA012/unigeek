#include "ChameleonMfcToolsScreen.h"
#include "ChameleonMfcScreen.h"
#include "ChameleonMfcWriteScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonMfcToolsScreen::onInit() {
  _items[0] = {"Read Tag"};
  _items[1] = {"Write to Tag"};
  _items[2] = {"Erase Tag [TODO]"};
  setItems(_items);
}

void ChameleonMfcToolsScreen::_writeFromFile() {
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
  const char* r = InputSelectAction::popup("Classic 1K dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count) { render(); return; }
  String path = _browser.entry(idx).path;
  render();
  Screen.push(new ChameleonMfcWriteScreen(path));
}

void ChameleonMfcToolsScreen::_writeFromSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) {
    render();
    ShowStatusAction::show("Could not read slots", 1500);
    render();
    return;
  }

  InputSelectAction::Option opts[8];
  String labels[8];
  String vals[8];
  for (uint8_t i = 0; i < 8; ++i) {
    labels[i] = String("Slot ") + (i + 1) + " - " +
                ChameleonClient::tagTypeName(types[i].hfType);
    vals[i] = String(i);
    opts[i] = {labels[i].c_str(), vals[i].c_str()};
  }

  const char* r = InputSelectAction::popup("Source slot", opts, 8, nullptr);
  if (!r) { render(); return; }
  const uint8_t slot = (uint8_t)atoi(r);
  if (slot >= 8) { render(); return; }

  const uint16_t hfType = types[slot].hfType;
  if (hfType == 0) {
    render();
    ShowStatusAction::show("Empty slot", 1200);
    render();
    return;
  }
  if (!(hfType == 1001)) {
    render();
    ShowStatusAction::show("Unsupported tag type", 1500);
    render();
    return;
  }

  Screen.push(new ChameleonMfcWriteScreen(slot));
}

void ChameleonMfcToolsScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"from File", "file"},
    {"from Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}

void ChameleonMfcToolsScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfcScreen());
  else if (index == 1) _writeTag();
  else if (index == 2) {
    render();
    ShowStatusAction::show("Not implemented yet", 1400);
    render();
  }
}

void ChameleonMfcToolsScreen::onBack() { Screen.goBack(); }
