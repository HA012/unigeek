#pragma once

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <WiFi.h>
#include <esp_wifi.h>

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class WifiWatchcatScreen : public BaseScreen
{
public:
  const char* title() override { return "WiFi Watchcat"; }
  bool inhibitPowerOff() override { return true; }

  ~WifiWatchcatScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack();

  using MacAddr = std::array<uint8_t, 6>;

  struct MacHash {
    size_t operator()(const MacAddr& m) const noexcept {
      uint64_t v = 0;
      memcpy(&v, m.data(), 6);
      return std::hash<uint64_t>{}(v);
    }
  };

  struct MacEqual {
    bool operator()(const MacAddr& a, const MacAddr& b) const noexcept {
      return memcmp(a.data(), b.data(), 6) == 0;
    }
  };

  struct ProbeEntry {
    char          ssids[3][33] = {};
    uint8_t       ssidCount    = 0;
    int           count        = 0;
    unsigned long timestamp    = 0;
  };

  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

private:
  static constexpr int           MAX_ITEMS       = 10;
  static constexpr int           MAX_ROWS        = 90;
  static constexpr unsigned long WINDOW_MS       = 30000UL;
  static constexpr int           MAX_RING        = 64;
  static constexpr size_t        MAX_TRACKED_MAC = 256;

  struct ProbeEvent {
    MacAddr       src;
    char          ssid[33];
    unsigned long timestamp;
  };

  static std::unordered_map<MacAddr, ProbeEntry, MacHash, MacEqual> _probeMap;
  static ProbeEvent   _probeRing[MAX_RING];
  static volatile int _probeRingHead;
  static volatile int _probeRingTail;
  static portMUX_TYPE _ringLock;

  int                 _channel    = 1;
  unsigned long       _lastUpdate = 0;
  int                 _itemCount  = 0;
  ScrollListView      _scroll;
  ScrollListView::Row _rows[MAX_ROWS]       = {};
  char                _labels[MAX_ROWS][64] = {};

  void _drainRing();
  void _renderProbes();
  void _setListState(int newCount);
};
