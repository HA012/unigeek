#include "ChameleonSlotViewScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/rfid/LFCodec.h"
#include "ChameleonSlotEditScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonSlotViewScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount]   = { _labels[_rowCount].c_str(), _values[_rowCount] };
  _rowCount++;
}

void ChameleonSlotViewScreen::_runHF() {
  auto& c = ChameleonClient::get();
  if (!c.setActiveSlot(_slot)) {
    _addRow("Error", "Failed");
    return;
  }
  delay(50);  // let firmware finish the slot switch before the next request

  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) {
    _addRow("Error", "Failed");
    return;
  }
  const uint16_t t = types[_slot].hfType;
  _addRow("Type", ChameleonClient::tagTypeName(t));

  if (t == 0) {
    _addRow("Data", "(empty)");
    return;
  }

  // MIFARE Classic emulator memory is exposed as 16-byte blocks.
  uint16_t totalBlocks = 0;
  if (t == 1000)      totalBlocks = 20;   // Mini
  else if (t == 1001) totalBlocks = 64;   // 1K
  else if (t == 1002) totalBlocks = 128;  // 2K
  else if (t == 1003) totalBlocks = 256;  // 4K

  if (totalBlocks != 0) {
    uint8_t buf[16];
    for (uint16_t b = 0; b < totalBlocks; b++) {
      uint16_t st = 0, rlen = 0;
      if (!c.mf1GetBlockData((uint8_t)b, 1, buf, &st, &rlen) || rlen < sizeof(buf)) {
        char diag[32];
        snprintf(diag, sizeof(diag), "@%u st=%u rlen=%u", b, st, rlen);
        _addRow("Error", diag);
        break;
      }

      char lbl[16];
      char hex[17];
      snprintf(lbl, sizeof(lbl), "B%u 0-7", (unsigned)b);
      snprintf(hex, sizeof(hex),
               "%02X%02X%02X%02X%02X%02X%02X%02X",
               buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);
      _addRow(lbl, hex);
      snprintf(lbl, sizeof(lbl), "B%u 8-F", (unsigned)b);
      snprintf(hex, sizeof(hex),
               "%02X%02X%02X%02X%02X%02X%02X%02X",
               buf[8],buf[9],buf[10],buf[11],buf[12],buf[13],buf[14],buf[15]);
      _addRow(lbl, hex);

      if ((b & 0x0F) == 0x0F) {
        _scrollView.setRows(_rows, _rowCount);
        render();
      }
    }
    return;
  }

  // MF0 / Ultralight / NTAG emulator memory is exposed as 4-byte pages.
  if (t >= 1100 && t <= 1108) {
    uint8_t totalPages = 0;
    if (!c.mfuGetPageCount(&totalPages) || totalPages == 0) {
      _addRow("Error", "Page count failed");
      return;
    }

    static constexpr uint8_t kPagesPerRead = 32;
    uint8_t buf[kPagesPerRead * 4] = {};

    for (uint16_t first = 0; first < totalPages; first += kPagesPerRead) {
      const uint8_t count =
          (uint8_t)min<uint16_t>(kPagesPerRead, (uint16_t)totalPages - first);
      const uint16_t expected = (uint16_t)count * 4u;
      uint16_t st = 0, rlen = 0;

      if (!c.mfuGetPageData((uint8_t)first, count, buf, &st, &rlen) ||
          rlen < expected) {
        char diag[32];
        snprintf(diag, sizeof(diag), "@%u st=%u rlen=%u", first, st, rlen);
        _addRow("Error", diag);
        break;
      }

      for (uint8_t i = 0; i < count; ++i) {
        const uint16_t page = first + i;
        const uint8_t* data = &buf[(uint16_t)i * 4u];

        char lbl[8];
        snprintf(lbl, sizeof(lbl), "P%03u", page);
        char hex[12];
        snprintf(hex, sizeof(hex), "%02X%02X%02X%02X",
                 data[0], data[1], data[2], data[3]);
        _addRow(lbl, hex);
      }

      _scrollView.setRows(_rows, _rowCount);
      render();
    }
    return;
  }

  _addRow("Data", "Unsupported");
}

void ChameleonSlotViewScreen::_runLF() {
  auto& c = ChameleonClient::get();
  if (!c.setActiveSlot(_slot)) { _addRow("Error", "Failed"); return; }
  delay(50);

  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) { _addRow("Error", "Failed"); return; }
  const uint16_t t = types[_slot].lfType;
  _addRow("Type", ChameleonClient::tagTypeName(t));
  if (t == 0) { _addRow("Data", "(empty)"); return; }

  const LFCodec::FormatInfo* info = LFCodec::fromChameleonType(t);
  if (!info) { _addRow("Data", "Unsupported"); return; }

  uint8_t data[LFCodec::kMaxDataSize] = {}, len = 0;
  bool ok = false;
  switch (info->protocol) {
    case LFCodec::Protocol::EM410X:     len = 5; ok = c.getEM410XSlot(data); break;
    case LFCodec::Protocol::HIDProx:    ok = c.getHIDProxSlot(data, &len); break;
    case LFCodec::Protocol::IoProx:     ok = c.getIoProxSlot(data, &len); break;
    case LFCodec::Protocol::Viking:     ok = c.getVikingSlot(data, &len); break;
    case LFCodec::Protocol::PACStanley: ok = c.getPACSlot(data, &len); break;
    case LFCodec::Protocol::Jablotron:  ok = c.getJablotronSlot(data, &len); break;
    default: break;
  }

  if (!ok || !LFCodec::validate(info->protocol, len)) {
    _addRow("Data", "(unavailable)");
    return;
  }
  _addRow("Data", LFCodec::hex(data, len, true));
}

void ChameleonSlotViewScreen::onInit() {
  auto& c = ChameleonClient::get();
  _restoreSlot = c.getActiveSlot(&_previousSlot) && _previousSlot != _slot;

  snprintf(_title, sizeof(_title), "Raw Data");
  _rowCount = 0;
  _loading  = true;
  _ready    = false;

  // Draw placeholder so user sees progress.
  _addRow("Reading", "...");
  _scrollView.setRows(_rows, _rowCount);
  render();

  _rowCount = 0;
  if (_lf) _runLF();
  else     _runHF();

  _scrollView.setRows(_rows, _rowCount);
  _loading = false;
  _ready   = true;
  render();

  int n = Achievement.inc("chameleon_slot_viewed");
  if (n == 1) Achievement.unlock("chameleon_slot_viewed");
}

void ChameleonSlotViewScreen::_restoreActiveSlot() {
  if (_restoreSlot) {
    ChameleonClient::get().setActiveSlot(_previousSlot);
    _restoreSlot = false;
  }
}

void ChameleonSlotViewScreen::onUpdate() {
  if (_loading) return;

  if (Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    Uni.Nav->suppressCurrentPress();
    _restoreActiveSlot();
    Screen.goBack();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      _restoreActiveSlot();
      Screen.goBack();
      return;
    }
    _scrollView.onNav(dir);
  }
}

void ChameleonSlotViewScreen::onRender() {
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}
