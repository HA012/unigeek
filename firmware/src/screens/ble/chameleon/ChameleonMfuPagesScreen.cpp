#include <new>
#include "ChameleonMfuAuthUtils.h"
#include "ChameleonMfuPagesScreen.h"
#include "screens/utility/NfcMemoryViewScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/TagPrompt.h"
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

void ChameleonMfuPagesScreen::_freeDump() {
  if (_dump && _ownsDump) free(_dump);
  _dump = nullptr;
  _dumpLen = 0;
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
  TagPrompt::show("Waiting for tag...", bx, by, bw, bh);

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
  _dump = (uint8_t*)calloc(1, bytes);
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
    render();
    ShowStatusAction::show("Failed", 1200);
    Screen.goBack();
    return;
  }

  _dumpLen = got;
  const uint16_t pages = (uint16_t)(bytes / 4u);
  uint8_t* valid = new(std::nothrow) uint8_t[pages];
  if (!valid) {
    _freeDump();
    ShowStatusAction::show("Out of memory", 1200);
    Screen.goBack();
    return;
  }
  memset(valid, 0, pages);
  const uint16_t gotPages = min<uint16_t>(pages, got / 4u);
  memset(valid, 1, gotPages);
  if (_info.type == ChameleonClient::MFU_ULTRALIGHT_C)
    for (uint16_t page = 44; page < pages; ++page) valid[page] = 0;
  _viewerPushed = true;
  Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::TYPE2,
                                      ChameleonClient::mfuTagTypeName(_info.type),
                                      _info.uid, _info.uidLen, _dump, bytes, valid));
  delete[] valid;
}

void ChameleonMfuPagesScreen::onInit() { _read(); }

void ChameleonMfuPagesScreen::onUpdate() {
  if (_busy) return;
  if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK)
    Screen.goBack();
}

void ChameleonMfuPagesScreen::onRender() {}

void ChameleonMfuPagesScreen::onRestore() {
  if (_viewerPushed) {
    _viewerPushed = false;
    _freeDump();
    Screen.goBack();
  }
}
