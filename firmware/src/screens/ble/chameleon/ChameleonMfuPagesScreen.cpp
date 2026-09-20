#include "ChameleonMfuPagesScreen.h"
#include "ChameleonMfuAuthUtils.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

namespace {
void pageReadProgress(uint16_t done, uint16_t total) {
  char msg[36];
  snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...", (unsigned)done, (unsigned)total);
  ProgressView::progress(msg, total ? (int)((uint32_t)done * 100u / total) : 0);
}

static bool waitForMfuTag(ChameleonClient& c, ChameleonClient::MfuTagInfo& info,
                          bool& tagPresent, uint32_t timeoutMs = 5000) {
  tagPresent = false;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK)
      return false;
    uint8_t uid[7]={}, uidLen=0, atqa[2]={}, sak=0;
    if (c.scan14A(uid,&uidLen,atqa,&sak)) {
      tagPresent = true;
      if (sak != 0x00) return false;
      return c.mfuDetect(&info);
    }
    delay(50);
  }
  return false;
}
}

void ChameleonMfuPagesScreen::_addRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _labels[_rowCount] = label;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), value};
  ++_rowCount;
}

void ChameleonMfuPagesScreen::_freeDump() {
  if (_dump && _ownsDump) free(_dump);
  _dump = nullptr;
  _dumpLen = 0;
}

void ChameleonMfuPagesScreen::_buildRows() {
  _rowCount = 0;
  _addRow("Type", ChameleonClient::mfuTagTypeName(_info.type));
  String uidText;
  for (uint8_t i = 0; i < _info.uidLen; ++i) {
    char b[4]; snprintf(b, sizeof(b), "%s%02X", i ? ":" : "", _info.uid[i]); uidText += b;
  }
  _addRow("UID", uidText);
  _addRow("Pages", String(_info.pages));
  const uint16_t pages = min<uint16_t>(_info.pages, _dumpLen / 4u);
  for (uint16_t page = 0; page < pages; ++page) {
    const uint8_t* d = _dump + page * 4u;
    if (_info.type == ChameleonClient::MFU_ULTRALIGHT_C && page >= 44) {
      char label[8]; snprintf(label, sizeof(label), "P%03u", (unsigned)page);
      _addRow(label, "Unreadable (key)");
    } else {
      char label[8]; snprintf(label, sizeof(label), "P%03u", (unsigned)page);
      char value[9]; snprintf(value, sizeof(value), "%02X%02X%02X%02X", d[0], d[1], d[2], d[3]);
      _addRow(label, value);
    }
  }
  _view.resetScroll();
  _view.setRows(_rows, _rowCount);
  _ready = true;
}

void ChameleonMfuPagesScreen::_read() {
  _busy = true;
  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  // Keep the standard header/sidebar visible while waiting for the tag.
  render();
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

  bool tagPresent = false;
  if (!waitForMfuTag(c, _info, tagPresent)) {
    if (restoreMode) c.setMode(previousMode);
    _busy = false;
    render();
    ShowStatusAction::show(tagPresent ? "Tag not supported" : "Tag not detected", 1200);
    Screen.goBack();
    return;
  }

  const uint32_t bytes = (uint32_t)_info.pages * 4u;
  _dump = (uint8_t*)malloc(bytes);
  if (!_dump) {
    if (restoreMode) c.setMode(previousMode);
    _busy = false;
    ShowStatusAction::show("Out of memory", 1200);
    Screen.goBack();
    return;
  }

  uint8_t pwd[4] = {}; bool usePwd = false;
  if (!ChameleonMfuAuthUtils::ensureForRange(c, _info, 0, _info.pages - 1, true, pwd, usePwd)) {
    free(_dump); _dump = nullptr;
    if (restoreMode) c.setMode(previousMode);
    _busy = false;
    render();
    return;
  }

  // Password/input actions may repaint outside the body. Restore the owning
  // screen chrome before ProgressView starts.
  render();
  ProgressView::init();
  uint16_t got = 0;
  const bool ok = c.mfuReadDump(_info, _dump, (uint16_t)bytes, &got, pageReadProgress,
                                usePwd ? pwd : nullptr, usePwd);
  ProgressView::finish();
  if (restoreMode) c.setMode(previousMode);
  _busy = false;

  if (!ok) {
    _freeDump();
    ShowStatusAction::show("Failed", 1200);
    Screen.goBack();
    return;
  }

  _dumpLen = got;
  _buildRows();
  render();
}

void ChameleonMfuPagesScreen::onInit() {
  if (_viewOnly) {
    _buildRows();
    render();
  } else {
    _read();
  }
}

void ChameleonMfuPagesScreen::onUpdate() {
  if (_busy) return;
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    _freeDump();
    Screen.goBack();
    return;
  }
  if (_ready) _view.onNav(dir);
}

void ChameleonMfuPagesScreen::onRender() {
  if (_ready) _view.render(bodyX(), bodyY(), bodyW(), bodyH());
}
