#include "WifiWatchcatScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"

#include <cstring>

std::unordered_map<WifiWatchcatScreen::MacAddr, WifiWatchcatScreen::ProbeEntry,
                   WifiWatchcatScreen::MacHash, WifiWatchcatScreen::MacEqual>
  WifiWatchcatScreen::_probeMap;

WifiWatchcatScreen::ProbeEvent WifiWatchcatScreen::_probeRing[MAX_RING] = {};
volatile int WifiWatchcatScreen::_probeRingHead = 0;
volatile int WifiWatchcatScreen::_probeRingTail = 0;
portMUX_TYPE WifiWatchcatScreen::_ringLock = portMUX_INITIALIZER_UNLOCKED;

WifiWatchcatScreen::~WifiWatchcatScreen()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  _probeMap.clear();
}

void WifiWatchcatScreen::onInit()
{
  _channel    = 1;
  _itemCount  = 0;
  _lastUpdate = millis();

  _probeRingHead = _probeRingTail = 0;
  _probeMap.clear();
  _scroll.resetScroll();

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiWatchcatScreen::_promiscuousCb);

  render();
}

void WifiWatchcatScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      onBack();
      return;
    }
    if (dir == INavigation::DIR_UP   || dir == INavigation::DIR_DOWN ||
        dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
      _scroll.onNav(dir);
    }
  }

  _drainRing();

  if (millis() - _lastUpdate >= 1000) {
    _lastUpdate = millis();
    _channel = (_channel % 13) + 1;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _renderProbes();
  }
}

void WifiWatchcatScreen::onRender()
{
  if (_itemCount > 0) {
    _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
  } else {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    Uni.Lcd.setTextDatum(MC_DATUM);
    Uni.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    Uni.Lcd.drawString(
      "Monitoring...",
      bodyX() + bodyW() / 2,
      bodyY() + bodyH() / 2
    );
  }
}

void WifiWatchcatScreen::onBack()
{
  Screen.goBack();
}

void WifiWatchcatScreen::_drainRing()
{
  for (int i = 0; i < MAX_RING && _probeRingTail != _probeRingHead; i++) {
    const auto& ev = _probeRing[_probeRingTail];
    MacAddr src{};
    memcpy(src.data(), ev.src.data(), 6);

    auto it = _probeMap.find(src);
    if (it == _probeMap.end()) {
      if (_probeMap.size() < MAX_TRACKED_MAC) {
        ProbeEntry e{};
        e.timestamp = ev.timestamp;
        e.count     = 1;
        if (ev.ssid[0] != '\0') {
          memcpy(e.ssids[0], ev.ssid, 33);
          e.ssidCount = 1;
        }
        _probeMap.emplace(src, e);
      }
      if (Achievement.inc("wifi_probe_logged") == 1)
        Achievement.unlock("wifi_probe_logged");
    } else {
      ++it->second.count;
      it->second.timestamp = ev.timestamp;
      if (ev.ssid[0] != '\0' && it->second.ssidCount < 3) {
        bool found = false;
        for (int j = 0; j < it->second.ssidCount; j++) {
          if (strcmp(it->second.ssids[j], ev.ssid) == 0) {
            found = true;
            break;
          }
        }
        if (!found)
          memcpy(it->second.ssids[it->second.ssidCount++], ev.ssid, 33);
      }
    }

    _probeRingTail = (_probeRingTail + 1) % MAX_RING;
  }
}

void WifiWatchcatScreen::_renderProbes()
{
  const unsigned long now = millis();

  {
    std::vector<MacAddr> toErase;
    for (auto& kv : _probeMap)
      if (now - kv.second.timestamp > WINDOW_MS) toErase.push_back(kv.first);
    for (auto& k : toErase) _probeMap.erase(k);
  }

  int n = 0;
  int macCount = 0;
  for (auto& kv : _probeMap) {
    if (macCount >= MAX_ITEMS || n >= MAX_ROWS) break;

    const MacAddr&    mac = kv.first;
    const ProbeEntry& e   = kv.second;

    snprintf(_labels[n], sizeof(_labels[n]),
             "%02X:%02X:%02X:%02X:%02X:%02X (x%d)",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], e.count);
    _rows[n].label = _labels[n];
    _rows[n].value = "";
    n++;

    if (e.ssidCount == 0) {
      if (n < MAX_ROWS) {
        snprintf(_labels[n], sizeof(_labels[n]), "  - (wildcard)");
        _rows[n].label = _labels[n];
        _rows[n].value = "";
        n++;
      }
    } else {
      for (int i = 0; i < e.ssidCount && n < MAX_ROWS; i++) {
        snprintf(_labels[n], sizeof(_labels[n]), "  - %s", e.ssids[i]);
        _rows[n].label = _labels[n];
        _rows[n].value = "";
        n++;
      }
    }

    macCount++;
  }

  _setListState(n);
}

void WifiWatchcatScreen::_setListState(int newCount)
{
  _itemCount = newCount;
  _scroll.setRows(_rows, (uint8_t)_itemCount);
  render();
}

void WifiWatchcatScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT || buf == nullptr) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;

  if (len < 26) return;

  const uint8_t fcSub  = (pay[0] >> 4) & 0x0F;
  const uint8_t fcType = (pay[0] >> 2) & 0x03;
  if (fcType != 0 || fcSub != 0x4) return;

  char ssid[33] = {};
  const uint8_t id   = pay[24];
  const uint8_t elen = pay[25];
  if (id == 0 && elen > 0 && elen <= 32 && (size_t)(26 + elen) <= len) {
    memcpy(ssid, pay + 26, elen);
    ssid[elen] = '\0';
  }

  portENTER_CRITICAL_ISR(&_ringLock);
  const int next = (_probeRingHead + 1) % MAX_RING;
  if (next != _probeRingTail) {
    memcpy(_probeRing[_probeRingHead].src.data(), pay + 10, 6);
    memcpy(_probeRing[_probeRingHead].ssid, ssid, 33);
    _probeRing[_probeRingHead].timestamp = millis();
    _probeRingHead = next;
  }
  portEXIT_CRITICAL_ISR(&_ringLock);
}
