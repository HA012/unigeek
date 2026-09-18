#include "ChameleonLFScanScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonLFScanScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= kMaxRows) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;
}

String ChameleonLFScanScreen::_hexData() const {
  String out;
  char h[3];
  for (uint8_t i = 0; i < _dataLen; ++i) {
    snprintf(h, sizeof(h), "%02X", _data[i]);
    out += h;
  }
  return out;
}

void ChameleonLFScanScreen::_draw() {
  _needsDraw = false;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  Sprite sp(&lcd);
  sp.createSprite(bw, bh);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  sp.drawString("Place tag on reader...", bw / 2, bh / 2);
  sp.pushSprite(bx, by);
  sp.deleteSprite();
}

void ChameleonLFScanScreen::_buildResult() {
  _rowCount = 0;
  const String hex = _hexData();

  switch (_protocol) {
    case EM410X: {
      uint64_t dec = 0;
      for (uint8_t i = 0; i < _dataLen; ++i) dec = (dec << 8) | _data[i];
      _addRow("UID (Hex)", hex);
      _addRow("UID (Dec)", String((unsigned long long)dec));
      _addRow("Format", "EM410X");
      break;
    }
    case HID_PROX:
      _addRow("Data", hex);
      _addRow("Format", "HID Prox");
      break;
    case IOPROX: {
      if (_dataLen >= 4) {
        _addRow("Facility", String(_data[1]));
        _addRow("Card Number", String((uint16_t(_data[2]) << 8) | _data[3]));
      }
      _addRow("Format", "ioProx");
      break;
    }
    case VIKING:
      _addRow("UID", hex);
      _addRow("Format", "Viking");
      break;
    case PAC_STANLEY: {
      bool printable = _dataLen > 0;
      String ascii;
      for (uint8_t i = 0; i < _dataLen; ++i) {
        if (_data[i] < 0x20 || _data[i] > 0x7E) printable = false;
        ascii += char(_data[i]);
      }
      _addRow(printable ? "ID" : "Data", printable ? ascii : hex);
      _addRow("Format", "PAC/Stanley");
      break;
    }
    case JABLOTRON:
      _addRow("ID", hex);
      _addRow("Format", "Jablotron");
      break;
    default:
      return;
  }

  _addRow("Frequency", "125 kHz");
  _addRow("[Press]", "Scan again");
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonLFScanScreen::_doScan() {
  _scanning = true;
  render();

  // Match Chameleon HF Scan Tag: keep the placement prompt visible while the
  // blocking reader calls run, rather than introducing a separate scan UI.
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  _protocol = NONE;
  _dataLen = 0;
  bool found = false;

  if (c.scanEM410X(_data)) {
    _protocol = EM410X; _dataLen = 5; found = true;
  } else if (c.scanHIDProx(_data, &_dataLen)) {
    _protocol = HID_PROX; found = true;
  } else if (c.scanIoProx(_data, &_dataLen)) {
    _protocol = IOPROX; found = true;
  } else if (c.scanViking(_data, &_dataLen)) {
    _protocol = VIKING; found = true;
  } else if (c.scanPAC(_data, &_dataLen)) {
    _protocol = PAC_STANLEY; found = true;
  } else if (c.scanJablotron(_data, &_dataLen)) {
    _protocol = JABLOTRON; found = true;
  }

  if (restoreMode) c.setMode(previousMode);
  _scanning = false;

  if (!found) {
    _state = STATE_IDLE;
    _needsDraw = true;
    render();
    ShowStatusAction::show("Tag not detected", 1200);
    // Match Chameleon HF Scan Tag: failed scan returns to the tools menu.
    Screen.goBack();
    return;
  }

  _state = STATE_RESULT;
  _buildResult();
  _needsDraw = true;
  render();
}

void ChameleonLFScanScreen::onInit() {
  _state = STATE_IDLE;
  _needsDraw = true;
  _doScan();
}

void ChameleonLFScanScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_needsDraw) _draw();
}

void ChameleonLFScanScreen::onUpdate() {
  if (_scanning) return;
  if (!Uni.Nav->wasPressed()) return;

  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    Screen.goBack();
    return;
  }
  if (dir == INavigation::DIR_PRESS) {
    _doScan();
    return;
  }
  if (_state == STATE_RESULT) _scrollView.onNav(dir);
}
