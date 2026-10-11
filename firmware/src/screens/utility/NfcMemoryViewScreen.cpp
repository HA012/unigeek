#include "NfcMemoryViewScreen.h"
#include <new>
#include "core/Device.h"
#include "core/ScreenManager.h"

NfcMemoryViewScreen::NfcMemoryViewScreen(Layout layout, const String& type,
                                         const uint8_t* uid, uint8_t uidLen,
                                         const uint8_t* data, size_t dataLen, const uint8_t* validUnits)
  : _layout(layout), _type(type), _uidLen(min<uint8_t>(uidLen, sizeof(_uid))), _dataLen(dataLen) {
  if (uid && _uidLen) memcpy(_uid, uid, _uidLen);
  if (data && dataLen) {
    _data = new(std::nothrow) uint8_t[dataLen];
    if (_data) memcpy(_data, data, dataLen);
    else _dataLen = 0;
  }
  _unitCount = _dataLen / (_layout == MIFARE_CLASSIC ? 16u : 4u);
  _validityProvided = validUnits != nullptr;
  if (_validityProvided && _unitCount) {
    _validUnits = new(std::nothrow) uint8_t[_unitCount];
    if (_validUnits) memcpy(_validUnits, validUnits, _unitCount);
  }
}

NfcMemoryViewScreen::~NfcMemoryViewScreen() { delete[] _data; delete[] _validUnits; }

void NfcMemoryViewScreen::addRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount].c_str()};
  ++_rowCount;
}

void NfcMemoryViewScreen::buildRows() {
  _rowCount = 0;
  addRow("Type", _type);
  String uidText;
  for (uint8_t i = 0; i < _uidLen; ++i) {
    char b[4]; snprintf(b, sizeof(b), "%s%02X", i ? ":" : "", _uid[i]); uidText += b;
  }
  addRow("UID", uidText.length() ? uidText : "Unknown");

  if (_layout == MIFARE_CLASSIC) {
    const size_t blocks = _dataLen / 16u;
    addRow("Blocks", String((unsigned)blocks));
    for (size_t block = 0; block < blocks && _rowCount + 1 < MAX_ROWS; ++block) {
      for (uint8_t half = 0; half < 2; ++half) {
        char label[16]; snprintf(label, sizeof(label), "B%u %s", (unsigned)block, half ? "8-F" : "0-7");
        String value;
        if (_validityProvided && (!_validUnits || !_validUnits[block])) {
          value = "????????????????";
        } else {
          char h[3];
          const uint8_t* p = _data + block * 16u + half * 8u;
          for (uint8_t i = 0; i < 8; ++i) { snprintf(h, sizeof(h), "%02X", p[i]); value += h; }
        }
        addRow(label, value);
      }
    }
  } else {
    const size_t pages = _dataLen / 4u;
    addRow("Pages", String((unsigned)pages));
    for (size_t page = 0; page < pages && _rowCount < MAX_ROWS; ++page) {
      char label[12]; snprintf(label, sizeof(label), "P%03u", (unsigned)page);
      if (_validityProvided && (!_validUnits || !_validUnits[page])) {
        addRow(label, "????????");
      } else {
        const uint8_t* p = _data + page * 4u;
        char value[9]; snprintf(value, sizeof(value), "%02X%02X%02X%02X", p[0], p[1], p[2], p[3]);
        addRow(label, value);
      }
    }
  }
  _view.resetScroll();
  _view.setRows(_rows, _rowCount);
}

void NfcMemoryViewScreen::onInit() { buildRows(); render(); }
void NfcMemoryViewScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  const auto d = Uni.Nav->readDirection();
  if (d == INavigation::DIR_BACK) { Screen.goBack(); return; }
  _view.onNav(d);
}
void NfcMemoryViewScreen::onRender() { _view.render(bodyX(), bodyY(), bodyW(), bodyH()); }
