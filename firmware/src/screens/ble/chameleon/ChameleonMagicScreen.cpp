#include "ChameleonMagicScreen.h"
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/components/TagPrompt.h"
#include "ui/views/ProgressView.h"
#include "utils/ble/ChameleonClient.h"

void ChameleonMagicScreen::onInit() {
  _phase = WAITING;
  _running = false;
  _started = false;
  _result = "";
  _needsDraw = true;
}

void ChameleonMagicScreen::onUpdate() {
  if (_running) return;

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK ||
        (dir == INavigation::DIR_PRESS && _phase == RESULT)) {
      Screen.goBack();
      return;
    }
  }

  if (!_started) {
    _started = true;
    _run();
  }
}

void ChameleonMagicScreen::onRender() {
  if (!_needsDraw) return;
  _needsDraw = false;

  if (_phase == WAITING) {
    TagPrompt::show("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_phase == RESULT) {
    auto& lcd = Uni.Lcd;
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(_result, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
  }
}

void ChameleonMagicScreen::_run() {
  _running = true;
  auto& c = ChameleonClient::get();

  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  uint8_t uid[7] = {}, uidLen = 0, atqa[2] = {}, sak = 0;
  const bool found = c.scan14A(uid, &uidLen, atqa, &sak);
  if (restoreMode) c.setMode(previousMode);

  if (!found) {
    _result = "Tag not detected";
    _phase = RESULT;
    _running = false;
    _needsDraw = true;
    onRender();
    return;
  }
  if (sak != 0x09 && sak != 0x08 && sak != 0x18) {
    _result = "Magic not detected";
    _phase = RESULT;
    _running = false;
    _needsDraw = true;
    onRender();
    return;
  }

  _phase = SCANNING;
  ProgressView::init();
  ProgressView::progress("Scanning...", 0);
  const MagicCardType magic = c.detectMagicType([](uint8_t p) { ProgressView::progress("Scanning...", p); });
  ProgressView::finish();

  _result = magic == MagicCardType::GEN1A ? "Gen1A" :
            magic == MagicCardType::GEN3 ? "Gen3" : "Magic not detected";
  if (magic != MagicCardType::NONE) {
    int n = Achievement.inc("chameleon_magic_detect");
    if (n == 1) Achievement.unlock("chameleon_magic_detect");
  }

  _phase = RESULT;
  _running = false;
  _needsDraw = true;
  onRender();
}
