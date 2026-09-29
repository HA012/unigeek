#include "ChameleonMfcBackdoorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/MfcBackdoorSENRecovery.h"
#include "utils/nfc/MfcKeyStore.h"
#include <cstring>

ChameleonMfcBackdoorScreen::ChameleonMfcBackdoorScreen(
    const uint8_t* uid, uint8_t uidLen, uint8_t sectors,
    const bool* foundA, const bool* foundB) {
  if (uid && uidLen) {
    _uidLen = uidLen > sizeof(_uid) ? sizeof(_uid) : uidLen;
    memcpy(_uid, uid, _uidLen);
  }
  _sectors = sectors && sectors <= 40 ? sectors : 16;
  if (foundA) memcpy(_foundA, foundA, sizeof(_foundA));
  if (foundB) memcpy(_foundB, foundB, sizeof(_foundB));
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
  add("Attack", "Backdoor Assisted SEN");
  add("Status", _status.length() ? _status : "Ready");
  add("[Press]", "Start");
  _scroll.setRows(_rows, _rowCount);
}

void ChameleonMfcBackdoorScreen::onInit() { _status = "Ready"; _build(); }

void ChameleonMfcBackdoorScreen::_run() {
  _busy = true;
  ProgressView::init();
  ProgressView::progress("Starting backdoor acquisition...", 30);
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
  auto progress = [](const char* msg, int pct) {
    ProgressView::progress(msg, pct);
  };
  const auto result = MfcBackdoorSENRecovery::run(
      _sectors, _foundA, _foundB, _keysA, _keysB, progress);
  if (restore) c.setMode(prev);
  ProgressView::finish();
  if (!result.acquired) {
    _status = "Backdoor not available";
  } else if (!result.success) {
    _status = "Backdoor acquired; no keys recovered";
  } else {
    _status = "Keys recovered";
  }
  memcpy(_foundA, result.foundA, sizeof(_foundA));
  memcpy(_foundB, result.foundB, sizeof(_foundB));
  memcpy(_keysA, result.keysA, sizeof(_keysA));
  memcpy(_keysB, result.keysB, sizeof(_keysB));

  // Persist SEN-recovered keys using the same store used by other MIFARE attacks.
  if (result.success && Uni.Storage && Uni.Storage->isAvailable()) {
    String buf;
    for (uint8_t s = 0; s < _sectors; ++s) {
      char line[48];
      if (_foundA[s]) {
        snprintf(line, sizeof(line), "S%02d A %02X%02X%02X%02X%02X%02X\n",
                 s, _keysA[s][0], _keysA[s][1], _keysA[s][2],
                 _keysA[s][3], _keysA[s][4], _keysA[s][5]);
        buf += line;
      }
      if (_foundB[s]) {
        snprintf(line, sizeof(line), "S%02d B %02X%02X%02X%02X%02X%02X\n",
                 s, _keysB[s][0], _keysB[s][1], _keysB[s][2],
                 _keysB[s][3], _keysB[s][4], _keysB[s][5]);
        buf += line;
      }
    }
    if (buf.length() > 0) {
      MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, buf);
    }
  }

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
