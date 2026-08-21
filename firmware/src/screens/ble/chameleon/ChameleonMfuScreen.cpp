#include "ChameleonMfuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/NdefParser.h"

void ChameleonMfuScreen::_freeDump() {
  if (_dump) {
    free(_dump);
    _dump = nullptr;
  }
  _dumpLen = 0;
}

void ChameleonMfuScreen::_drawIdle() {
  _needsDraw = false;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  Sprite sp(&lcd);
  sp.createSprite(bw, bh);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_CYAN, TFT_BLACK);
  sp.drawString("Ultralight / NTAG", bw / 2, bh / 2 - 28);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Place tag near CU reader", bw / 2, bh / 2 - 8);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString("[Press] Read", bw / 2, bh / 2 + 18);
  sp.pushSprite(bx, by);
  sp.deleteSprite();
}

void ChameleonMfuScreen::_buildResult() {
  _rowCount = 0;

  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= kMaxRows) return false;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(),
                        _rowValues[_rowCount].c_str()};
    ++_rowCount;
    return true;
  };

  char uid[24] = {};
  for (uint8_t i = 0; i < _info.uidLen; ++i) {
    char b[4];
    snprintf(b, sizeof(b), "%02X%s", _info.uid[i],
             (i + 1 < _info.uidLen) ? ":" : "");
    strcat(uid, b);
  }

  addRow("Type", ChameleonClient::mfuTagTypeName(_info.type));
  addRow("UID", uid);
  addRow("Pages", String(_info.pages));
  addRow("Dump", String(_dumpLen) + " bytes");

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;

  if (NdefParser::extractType2Ndef(_dump, _dumpLen, &ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        addRow("NDEF", "Text");
        if (parsed.language.length()) addRow("Language", parsed.language);
        if (parsed.text.length()) addRow("Text", parsed.text);
        break;

      case NdefParser::RECORD_URL:
        addRow("NDEF", "URL");
        addRow("URL", parsed.uri);
        break;

      case NdefParser::RECORD_PHONE:
        addRow("NDEF", "Phone");
        addRow("Phone", parsed.phone);
        break;

      case NdefParser::RECORD_EMAIL:
        addRow("NDEF", "Email");
        addRow("Email", parsed.email);
        break;

      case NdefParser::RECORD_VCARD:
        addRow("NDEF", "vCard");
        if (parsed.contact.length()) addRow("Contact", parsed.contact);
        if (parsed.company.length()) addRow("Company", parsed.company);
        if (parsed.address.length()) addRow("Address", parsed.address);
        if (parsed.phone.length()) addRow("Phone", parsed.phone);
        if (parsed.email.length()) addRow("Email", parsed.email);
        if (parsed.website.length()) addRow("Website", parsed.website);
        break;

      default:
        addRow("NDEF", "Unsupported");
        break;
    }
  } else {
    addRow("NDEF", "Not found");
  }

  addRow("[Press]", "Read again");
  addRow("[Hold]", "Save dump");

  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonMfuScreen::_read() {
  _busy = true;
  _freeDump();

  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Detecting tag...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  c.setMode(1);

  if (!c.mfuDetect(&_info)) {
    _busy = false;
    _state = STATE_IDLE;
    _needsDraw = true;
    render();
    ShowStatusAction::show("Unsupported / no tag", 1400);
    render();
    return;
  }

  const uint32_t total = (uint32_t)_info.pages * 4u;
  _dump = (uint8_t*)malloc(total);
  if (!_dump) {
    _busy = false;
    _state = STATE_IDLE;
    _needsDraw = true;
    render();
    ShowStatusAction::show("Out of memory", 1200);
    render();
    return;
  }

  ProgressView::init();
  ProgressView::progress("Reading tag", 0);

  uint16_t got = 0;
  bool ok = c.mfuReadDump(_info, _dump, (uint16_t)total, &got);
  ProgressView::finish();

  _busy = false;
  if (!ok) {
    _freeDump();
    _state = STATE_IDLE;
    _needsDraw = true;
    render();
    ShowStatusAction::show("Read failed", 1200);
    render();
    return;
  }

  _dumpLen = got;
  _state = STATE_RESULT;
  _buildResult();
  _needsDraw = true;
  render();
}

void ChameleonMfuScreen::_save() {
  if (!_dump || !_dumpLen || !Uni.Storage) return;

  // Keep the save-name convention consistent with the other NFC dump
  // workflows: suggest the UID as the editable basename. The .bin extension
  // is deliberately not shown in the editor; it is appended only on save.
  char suggested[24] = {};
  size_t pos = 0;
  for (uint8_t i = 0; i < _info.uidLen && pos + 2 < sizeof(suggested); ++i) {
    pos += snprintf(suggested + pos, sizeof(suggested) - pos,
                    "%02X", _info.uid[i]);
  }

  String name = InputTextAction::popup("Save dump", suggested);
  if (!name.length()) { render(); return; }

  // Treat the edited value as a basename. Do not expose the extension in the
  // editor; add it only when constructing the actual filename.
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
    ShowStatusAction::show(msg.c_str(), 1500);
  } else {
    ShowStatusAction::show("Save failed", 1200);
  }
  render();
}

void ChameleonMfuScreen::onInit() {
  _state = STATE_IDLE;
  _needsDraw = true;
}

void ChameleonMfuScreen::onUpdate() {
  if (_busy) return;

  if (!_holdFired && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 700) {
    _holdFired = true;
    Uni.Nav->suppressCurrentPress();
    if (_state == STATE_RESULT) _save();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      _freeDump();
      Screen.goBack();
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      _read();
      return;
    }
    if (_state == STATE_RESULT) _scrollView.onNav(dir);
  } else if (_holdFired && !Uni.Nav->isPressed()) {
    _holdFired = false;
  }
}

void ChameleonMfuScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_needsDraw) _drawIdle();
}
