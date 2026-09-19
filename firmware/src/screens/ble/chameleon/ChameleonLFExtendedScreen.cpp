#include "ChameleonLFExtendedScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <stdio.h>
#include <string.h>

const char* ChameleonLFExtendedScreen::_protocolName() const {
  switch (_protocol) {
    case IOPROX: return "ioProx";
    case PAC_STANLEY: return "PAC/Stanley";
    default: return "Jablotron";
  }
}

const char* ChameleonLFExtendedScreen::title() {
  return _state == STATE_RESULT ? "Tag Details" : _protocolName();
}

void ChameleonLFExtendedScreen::_buildResult() {
  _rowCount = 0;
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };

  if (_protocol == IOPROX && _dataLen >= 4) {
    uint16_t cn = ((uint16_t)_data[2] << 8) | _data[3];
    addRow("Facility", String(_data[1]));
    addRow("Card Number", String(cn));
  } else {
    String value;
    if (_protocol == PAC_STANLEY) {
      bool printable = true;
      for (uint8_t i = 0; i < _dataLen; i++) {
        if (_data[i] < 32 || _data[i] > 126) { printable = false; break; }
      }
      if (printable) {
        for (uint8_t i = 0; i < _dataLen; i++) value += (char)_data[i];
      }
    }
    if (!value.length()) {
      char h[3];
      for (uint8_t i = 0; i < _dataLen; i++) {
        snprintf(h, sizeof(h), "%02X", _data[i]);
        value += h;
      }
    }
    addRow("Data", value);
  }
  addRow("Format", _protocolName());
  addRow("Frequency", "125 kHz");
  addRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonLFExtendedScreen::onInit() {
  _state = STATE_IDLE;
  _needsDraw = true;
  _doScan();
}

void ChameleonLFExtendedScreen::onRender() {
  if (_state == STATE_RESULT) _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}

void ChameleonLFExtendedScreen::_doScan() {
  _scanning = true;

  // onInit() starts the blocking scan before BaseScreen gets its first render.
  // Draw the full screen first so the header/sidebar stay visible while waiting.
  render();
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
  bool ok = false;
  _dataLen = 0;
  if (_protocol == IOPROX) ok = c.scanIoProx(_data, &_dataLen);
  else if (_protocol == PAC_STANLEY) ok = c.scanPAC(_data, &_dataLen);
  else ok = c.scanJablotron(_data, &_dataLen);

  _scanning = false;
  _state = ok ? STATE_RESULT : STATE_IDLE;
  if (ok) {
    _buildResult();
    if (restoreMode) c.setMode(previousMode);
  }
  _needsDraw = true;
  render();
  if (!ok) { if (restoreMode) c.setMode(previousMode); ShowStatusAction::show("Tag not detected", 1200); Screen.goBack(); }
}

void ChameleonLFExtendedScreen::_doLoadSlot() {
  auto& c = ChameleonClient::get();
  bool ok = false;
  if (_protocol == IOPROX) ok = c.setIoProxSlot(_data);
  else if (_protocol == PAC_STANLEY) ok = c.setPACSlot(_data);
  else ok = c.setJablotronSlot(_data);

  if (ok) {
    c.setMode(0);
    int n = Achievement.inc("chameleon_clone");
    if (n == 1) Achievement.unlock("chameleon_clone");
    if (n == 3) Achievement.unlock("chameleon_clone_3");
    if (n == 10) Achievement.unlock("chameleon_clone_10");
  }
  _state = STATE_RESULT; _needsDraw = true; render();
  ShowStatusAction::show(ok ? "Loaded to slot" : "Failed", 1200); render();
}

void ChameleonLFExtendedScreen::_saveToFile() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_dataLen) {
    ShowStatusAction::show("Failed", 1200);
    render();
    return;
  }

  String type = _protocolName();
  // Keep the canonical CU type recognizable while making it path-safe.
  type.replace("/", "-");
  type.replace(" ", "-");

  String suggested = type + "_";
  char h[3];
  for (uint8_t i = 0; i < (uint8_t)(_dataLen); ++i) {
    snprintf(h, sizeof(h), "%02X", _data[i]);
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
    ok = f.write(_data, _dataLen) == (size_t)(_dataLen);
    f.close();
  }

  render();
  if (ok) {
    String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1600);
  } else {
    ShowStatusAction::show("Failed", 1200);
  }
  render();
}

void ChameleonLFExtendedScreen::_doWriteTag() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM); lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Writing tag...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);
  bool ok = false;
  if (_protocol == IOPROX) ok = c.writeIoProxToT5577(_data, nullptr, nullptr, 0);
  else if (_protocol == PAC_STANLEY) ok = c.writePACToT5577(_data, nullptr, nullptr, 0);
  else ok = c.writeJablotronToT5577(_data, nullptr, nullptr, 0);

  if (restoreMode) c.setMode(previousMode);
  if (ok) { int n = Achievement.inc("chameleon_t5577_write"); if (n == 1) Achievement.unlock("chameleon_t5577_write"); }
  _state = STATE_RESULT; _needsDraw = true; render();
  ShowStatusAction::show(ok ? "Tag written" : "Failed", 1600); render();
}

void ChameleonLFExtendedScreen::_showActions() {
  static const InputSelectAction::Option opts[] = {
    {"Load to Slot", "slot"}, {"Save to File", "save"}, {"Write to Tag (T5577)", "t5577"},
  };
  String popup = String(_protocolName()) + " Actions";
  const char* r = InputSelectAction::popup(popup.c_str(), opts, 3, nullptr);
  if (r && strcmp(r, "slot") == 0) _doLoadSlot();
  else if (r && strcmp(r, "save") == 0) _saveToFile();
  else if (r && strcmp(r, "t5577") == 0) _doWriteTag();
  else render();
}

void ChameleonLFExtendedScreen::onUpdate() {
  if (_scanning) return;
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
  if (_state != STATE_RESULT) return;
  if (dir == INavigation::DIR_PRESS) { _showActions(); return; }
  _scrollView.onNav(dir);
}
