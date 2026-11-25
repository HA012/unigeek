#include "WifiQuietDosScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/network/WifiAttackUtil.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

// Quiet duration presets, in Time Units (1 TU = 1024 us). Longer = more silence.
static const uint16_t kDur[3]      = {200, 1000, 5000};
static const char*    kDurName[3]  = {"Short", "Medium", "Long"};

WifiQuietDosScreen::~WifiQuietDosScreen()
{
  if (_attacker) { delete _attacker; _attacker = nullptr; }
}

void WifiQuietDosScreen::configureFromChannel(uint8_t channel, const uint8_t* bssid, const char* ssid)
{
  _fromContext = true;
  if (bssid) {
    memcpy(_target.bssid, bssid, 6);
    _target.channel = (channel >= 1 && channel <= 13) ? (int)channel : 1;
    _target.ssid = (ssid && ssid[0]) ? String(ssid) : String("-");
  }
}

void WifiQuietDosScreen::onInit()
{
  if (_fromContext) {
    _startAttack();
    if (_state != STATE_RUNNING) { _fromContext = false; _showMain(); }
    return;
  }
  _showMain();
}

void WifiQuietDosScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MAIN) {
    _mainSel = index;
    if (index == 0) _selectWifi();
    else if (index == 1) { _durIdx = (uint8_t)((_durIdx + 1) % 3); _showMain(); }
    else if (index == 2) _startAttack();
  } else if (_state == STATE_SELECT_WIFI) {
    if (index == 0) { _selectWifi(true); return; }
    const int scanIndex = (int)index - 1;
    if (scanIndex < 0 || scanIndex >= _scanCount) return;
    _target.ssid    = _scanLabels[scanIndex];
    _target.channel = _scanChannels[scanIndex];
    sscanf(_scanValues[scanIndex], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &_target.bssid[0], &_target.bssid[1], &_target.bssid[2],
           &_target.bssid[3], &_target.bssid[4], &_target.bssid[5]);
    _showMain();
  }
}

void WifiQuietDosScreen::onUpdate()
{
  if (_state != STATE_RUNNING) { ListScreen::onUpdate(); return; }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) { _stopAttack(); return; }
  }

  if (_attacker) {
    _attacker->quiet(_target.bssid, _target.ssid.c_str(), _target.channel, 2, 1, kDur[_durIdx], 0);
    _statusTitle  = "Quiet DoS";
    _statusDetail = _target.ssid;
  }
  render();
  delay(100);
}

void WifiQuietDosScreen::onBack()
{
  if (_state == STATE_SELECT_WIFI)      _showMain();
  else if (_state == STATE_RUNNING)     _stopAttack();
  else                                  Screen.goBack();
}

void WifiQuietDosScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawStatus(_statusTitle.c_str(), _statusDetail.c_str()); return; }
  ListScreen::onRender();
}

void WifiQuietDosScreen::_showMain()
{
  _state = STATE_MAIN;
  _targetSub = _target.ssid;
  _durSub    = String(kDurName[_durIdx]) + " (" + String(kDur[_durIdx]) + " TU)";
  _mainItems[0] = {"Access Point", _targetSub.c_str()};
  _mainItems[1] = {"Duration",     _durSub.c_str()};
  _mainItems[2] = {"Start"};
  if (_mainSel >= 3) _mainSel = 0;
  setItems(_mainItems, 3, _mainSel);
}

void WifiQuietDosScreen::_showScanResults()
{
  _state = STATE_SELECT_WIFI;
  _scanItems[0] = {"Rescan"};
  for (int i = 0; i < _scanCount; i++) {
    _scanItems[i + 1] = {_scanLabels[i], _scanDisplay[i]};
    _scanItems[i + 1].rssi            = _scanRssi[i];
    _scanItems[i + 1].hasRssi         = true;
    _scanItems[i + 1].sublabelMarquee = true;
  }
  setItems(_scanItems, _scanCount + 1);
}

void WifiQuietDosScreen::_selectWifi(bool forceScan)
{
  if (_scanValid && !forceScan) { _showScanResults(); return; }

  _state = STATE_SELECT_WIFI;
  render();
  ShowStatusAction::show("Scanning...", 0);

  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  const int total = WiFi.scanNetworks();
  _scanCount = total > MAX_SCAN ? MAX_SCAN : (total > 0 ? total : 0);
  for (int i = 0; i < _scanCount; i++) {
    snprintf(_scanLabels[i], sizeof(_scanLabels[i]), "%s", WiFi.SSID(i).c_str());
    _scanChannels[i] = (uint8_t)WiFi.channel(i);
    snprintf(_scanValues[i], sizeof(_scanValues[i]), "%s", WiFi.BSSIDstr(i).c_str());
    snprintf(_scanDisplay[i], sizeof(_scanDisplay[i]), "%s CH%u", _scanValues[i], (unsigned)_scanChannels[i]);
    _scanRssi[i] = (int16_t)WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  _scanValid = true;

  if (_scanCount == 0) ShowStatusAction::show("No networks found");
  _showScanResults();
}

void WifiQuietDosScreen::_startAttack()
{
  const uint8_t blank[6] = {0, 0, 0, 0, 0, 0};
  if (_target.ssid == "-" && memcmp(_target.bssid, blank, 6) == 0) {
    ShowStatusAction::show("Access Point not set");
    return;
  }

  _state       = STATE_RUNNING;
  _chromeDrawn = false;
  _statusTitle  = "Quiet DoS";
  _statusDetail = _target.ssid;
  render();

  _attacker = new WifiAttackUtil();
  render();
}

void WifiQuietDosScreen::_stopAttack()
{
  if (_attacker) { delete _attacker; _attacker = nullptr; }
  ShowStatusAction::show("Stopped", 1000);
  if (_fromContext) { _fromContext = false; Screen.goBack(); }
  else              _showMain();
}

void WifiQuietDosScreen::_drawStatus(const char* titleLine, const char* detailLine)
{
  auto& lcd = Uni.Lcd;
  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _chromeDrawn = true;
  }

  const int lineH  = lcd.fontHeight();
  const int labelH = lineH * 2 + 6;
  const int cy     = bodyY() + bodyH() / 2;

  Sprite sp(&Uni.Lcd);
  if (!sp.createSprite(bodyW(), labelH)) return;
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(MC_DATUM);

  const int maxTextW = bodyW() - 8;
  auto fit = [&](String t) -> String {
    if (sp.textWidth(t.c_str()) <= maxTextW) return t;
    while (t.length() > 1 && sp.textWidth((t + "...").c_str()) > maxTextW) t.remove(t.length() - 1);
    return t + "...";
  };

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(fit(String(titleLine)).c_str(), bodyW() / 2, lineH / 2 + 1);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(fit(String(detailLine)).c_str(), bodyW() / 2, lineH + lineH / 2 + 5);

  sp.pushSprite(bodyX(), cy - labelH / 2);
  sp.deleteSprite();
}
