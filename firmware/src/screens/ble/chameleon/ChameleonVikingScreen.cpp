#include "ChameleonVikingScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <stdio.h>
#include <string.h>

void ChameleonVikingScreen::_buildResult() {
  _rowCount = 0;
  String hex;
  char h[3];
  for (uint8_t i = 0; i < _uidLen; i++) {
    snprintf(h, sizeof(h), "%02X", _uid[i]);
    hex += h;
  }
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };
  addRow("Data", hex);
  addRow("Format", "Viking");
  addRow("Frequency", "125 kHz");
  addRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonVikingScreen::onInit() {
  _state = STATE_IDLE;
  _needsDraw = true;
  _doScan();
}

void ChameleonVikingScreen::onRender() {
  if (_state == STATE_RESULT) _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}

void ChameleonVikingScreen::_doScan() {
  _scanning = true;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);
  bool ok = c.scanViking(_uid, &_uidLen);
  _scanning = false;
  _state = ok ? STATE_RESULT : STATE_IDLE;

  if (ok) {
    _buildResult();
    int n = Achievement.inc("chameleon_viking_scan");
    if (n == 1) Achievement.unlock("chameleon_viking_scan");
    if (_operation == LOAD_TO_SLOT) { _doLoadSlot(); return; }
    if (restoreMode) c.setMode(previousMode);
    if (_operation == WRITE_T5577) { _doT5577(); return; }
  }
  _needsDraw = true;
  render();
  if (!ok) { if (restoreMode) c.setMode(previousMode); ShowStatusAction::show("Tag not detected", 1200); Screen.goBack(); }
}

void ChameleonVikingScreen::_doLoadSlot() {
  auto& c = ChameleonClient::get();
  bool ok = c.setVikingSlot(_uid, _uidLen);
  if (ok) {
    c.setMode(0);
    int n = Achievement.inc("chameleon_clone");
    if (n == 1) Achievement.unlock("chameleon_clone");
    if (n == 3) Achievement.unlock("chameleon_clone_3");
    if (n == 10) Achievement.unlock("chameleon_clone_10");
  }
  _state = STATE_RESULT; _needsDraw = true; render();
  ShowStatusAction::show(ok ? "Loaded to slot" : "Load to slot failed", 1200); render();
}

void ChameleonVikingScreen::_saveToFile() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_uidLen) {
    ShowStatusAction::show("Save failed", 1200);
    render();
    return;
  }

  String type = "Viking";
  // Keep the canonical CU type recognizable while making it path-safe.
  type.replace("/", "-");
  type.replace(" ", "-");

  String suggested = type + "_";
  char h[3];
  for (uint8_t i = 0; i < (uint8_t)(_uidLen); ++i) {
    snprintf(h, sizeof(h), "%02X", _uid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("File name", suggested.c_str());
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  render();

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  String filename = name + ".bin";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/rfid");
  String path = String("/unigeek/rfid/") + filename;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (f) {
    ok = f.write(_uid, _uidLen) == (size_t)(_uidLen);
    f.close();
  }

  render();
  if (ok) {
    String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1600);
  } else {
    ShowStatusAction::show("Save failed", 1200);
  }
  render();
}

void ChameleonVikingScreen::_doT5577() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM); lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Writing tag...", bx + bw / 2, by + bh / 2);
  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);
  bool ok = c.writeVikingToT5577(_uid, nullptr, nullptr, 0);
  if (restoreMode) c.setMode(previousMode);
  if (ok) { int n = Achievement.inc("chameleon_t5577_write"); if (n == 1) Achievement.unlock("chameleon_t5577_write"); }
  _state = STATE_RESULT; _needsDraw = true; render();
  ShowStatusAction::show(ok ? "Tag written" : "Tag write failed", 1600); render();
}

void ChameleonVikingScreen::_showActions() {
  static const InputSelectAction::Option opts[] = {
    {"Load to Slot", "slot"}, {"Save to File", "save"}, {"Write to Tag (T5577)", "t5577"},
  };
  const char* r = InputSelectAction::popup("Viking Actions", opts, 3, nullptr);
  if (r && strcmp(r, "slot") == 0) _doLoadSlot();
  else if (r && strcmp(r, "save") == 0) _saveToFile();
  else if (r && strcmp(r, "t5577") == 0) _doT5577();
  else render();
}

void ChameleonVikingScreen::onUpdate() {
  if (_scanning) return;
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
  if (_state != STATE_RESULT) return;
  if (dir == INavigation::DIR_PRESS) { _showActions(); return; }
  _scrollView.onNav(dir);
}
