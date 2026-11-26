#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"
#include "AttackMonitor.h"

// ── CSA Spoof (802.11h Channel Switch Announcement DoS) ─────────────────────
// Tells clients their AP is moving to another channel, so those honouring
// 802.11h follow to a channel where the AP is not and drop the link. Mirrors
// WifiDeauthDisassocScreen: Mode Target/All, up to 4 client targets, AP + client
// scans. Broadcast form = beacon with the CSA element (whole BSS); targeted form
// = unicast CSA action frame per selected client.
class WifiCsaSpoofScreen : public ListScreen
{
public:
  const char* title() override { return "CSA Spoof"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  WifiCsaSpoofScreen() {
    memset(_mainItems,  0, sizeof(_mainItems));
    memset(_scanItems,  0, sizeof(_scanItems));
    memset(_scanLabels,   0, sizeof(_scanLabels));
    memset(_scanValues,   0, sizeof(_scanValues));
    memset(_scanChannels, 0, sizeof(_scanChannels));
  }
  ~WifiCsaSpoofScreen() override;

  // Contextual entry from WiFi Scanner / Traffic Monitor.
  void configureFromChannel(uint8_t channel, const uint8_t* bssid,
                            const uint8_t* client = nullptr, const char* ssid = nullptr);

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  typedef uint8_t MacAddr[6];

  struct Target {
    String  ssid    = "-";
    MacAddr bssid   = {0, 0, 0, 0, 0, 0};
    int     channel = 1;
  };

  struct ApEntry {
    uint8_t bssid[6];
    uint8_t channel;
    bool    valid;
  };

  enum State { STATE_MAIN, STATE_SELECT_WIFI, STATE_SELECT_CLIENTS, STATE_RUNNING };
  enum Mode  { MODE_TARGET, MODE_ALL };

  static constexpr uint8_t kChannelShift = 5;  // AP channel + this (wrapped) = redirect

  State  _state = STATE_MAIN;
  Mode   _mode  = MODE_TARGET;
  Target _target;
  uint8_t _toChannel = 1;     // redirect channel, derived from the AP's (Target mode)
  uint8_t _mainSel = 0;
  bool _fromContext = false;

  class WifiAttackUtil* _attacker = nullptr;

  static constexpr int MAX_SCAN = 20;
  static constexpr int MAX_ALL  = 30;

  uint8_t _allChanHop = 0;
  bool    _chromeDrawn = false;

  ListItem _mainItems[4];
  String _clientsSub;

  static constexpr uint8_t MAX_SELECTED_CLIENTS = 4;
  static constexpr uint8_t MAX_SEEN_CLIENTS = 20;
  struct ClientEntry { uint8_t mac[6]; int8_t rssi; char macStr[18]; };
  ClientEntry _seenClients[MAX_SEEN_CLIENTS] = {};
  uint8_t _seenClientCount = 0;
  uint8_t _clientOrder[MAX_SEEN_CLIENTS] = {};
  uint8_t _selectedClients[MAX_SELECTED_CLIENTS][6] = {};
  uint8_t _selectedClientCount = 0;
  char _selectedClientMacStr[18] = {};
  ListItem _clientItems[MAX_SEEN_CLIENTS + 2];
  char _clientLabels[MAX_SEEN_CLIENTS + 2][32] = {};
  static WifiCsaSpoofScreen* _clientScanner;
  static portMUX_TYPE _clientLock;
  static void _clientCb(void* buf, wifi_promiscuous_pkt_type_t type);
  void _scanClients();
  void _showClients(bool preserveSelection = false);
  bool _clientSelected(const uint8_t* mac) const;
  void _refreshSelectedClientMacStr();
  String   _modeSub;
  String   _targetSub;

  ListItem _scanItems[MAX_SCAN + 1];
  char     _scanLabels[MAX_SCAN][52];
  char     _scanValues[MAX_SCAN][18];
  char     _scanDisplay[MAX_SCAN][24];
  int16_t  _scanRssi[MAX_SCAN] = {};
  uint8_t  _scanChannels[MAX_SCAN] = {};
  int      _scanCount = 0;
  bool     _scanValid = false;

  static ApEntry      _allTargets[MAX_ALL];
  static int          _allCount;
  static portMUX_TYPE _allLock;
  static void _beaconCb(void* buf, wifi_promiscuous_pkt_type_t type);

  String _statusTitle;
  String _statusDetail;

  AttackMonitor _monitor;   // Target-mode live feedback

  void _showMain();
  void _selectWifi(bool forceScan = false);
  void _showScanResults();
  void _defaultToChannel();
  static uint8_t _shiftChannel(uint8_t ch);
  void _startAttack();
  void _stopAttack();
  void _csaAll();
  void _drawStatus(const char* titleLine, const char* detailLine);
};
