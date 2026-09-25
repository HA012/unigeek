#include "ChameleonMfcBackdoorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include <cstring>

ChameleonMfcBackdoorScreen::ChameleonMfcBackdoorScreen(
    const uint8_t* uid, uint8_t uidLen, uint8_t sectors,
    const bool* foundA, const bool* foundB) {
  if (uid && uidLen) {
    _uidLen = uidLen > sizeof(_uid) ? sizeof(_uid) : uidLen;
    memcpy(_uid, uid, _uidLen);
  }
  _sectors = sectors && sectors <= 40 ? sectors : 16;
  (void)foundA; (void)foundB;
}

void ChameleonMfcBackdoorScreen::_build() {
  _rowCount = 0;
  auto add = [&](const char* l, const String& v) {
    if (_rowCount >= 6) return;
    _labels[_rowCount] = l;
    _values[_rowCount] = v;
    _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount]};
    ++_rowCount;
  };
  add("Attack", "Backdoor / SEN");
  add("Status", _status.length() ? _status : "Ready");
  add("[Press]", "Start");
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcBackdoorScreen::onInit() { _status = "Ready"; _build(); }

void ChameleonMfcBackdoorScreen::_run() {
  _busy = true;
  ProgressView::init();
  ProgressView::progress("Probing tag...", 30);
  auto& c = ChameleonClient::get();
  uint8_t prev = 0;
  const bool restore = c.getMode(&prev);
  if (!c.setMode(1)) {
    _status = "Device not in reader mode";
    if (restore) c.setMode(prev);
    ProgressView::finish();
    _busy = false;
    return;
  }
  ProgressView::progress("Checking MF1...", 70);
  const bool mf1 = c.mf1Support();
  if (restore) c.setMode(prev);
  ProgressView::finish();
  _status = mf1 ? "SEN acquire not in client" : "Not MIFARE Classic";
  // Rebuild the screen after ProgressView before placing the modal status on
  // top; otherwise remnants of the completed progress view can show through.
  _build();
  render();
  ShowStatusAction::show(_status.c_str(), 1800);
  _busy = false;
}

void ChameleonMfcBackdoorScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  const auto d = Uni.Nav->readDirection();
  if (d == INavigation::DIR_BACK) { Screen.goBack(); return; }
  if (_busy) return;
  if (d == INavigation::DIR_PRESS) { _run(); _build(); render(); return; }
  _scroll.onNav(d);
}

void ChameleonMfcBackdoorScreen::onRender() {
  _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
}
