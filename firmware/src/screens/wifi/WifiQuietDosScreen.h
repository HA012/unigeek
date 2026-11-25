#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"

// ── Quiet DoS (802.11h Quiet element) ───────────────────────────────────────
// Beacons spoofing a target AP that carry a Quiet element asking stations to
// withhold transmissions, degrading the BSS. Clients honouring 802.11h fall
// silent. Mirrors WifiDeauthDisassocScreen's Mode Target/All; no per-client
// targeting — the Quiet element has no unicast form, so it is always broadcast.
class WifiQuietDosScreen : public ListScreen
{
public:
  const char* title() override { return "Quiet DoS"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  WifiQuietDosScreen() {
    memset(_mainItems,  0, sizeof(_mainItems));
    memset(_scanItems,  0, sizeof(_scanItems));
    memset(_scanLabels,   0, sizeof(_scanLabels));
    memset(_scanValues,   0, sizeof(_scanValues));
    memset(_scanChannels, 0, sizeof(_scanChannels));
  }
  ~WifiQuietDosScreen() override;

  void configureFromChannel(uint8_t channel, const uint8_t* bssid, const char* ssid = nullptr);

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  typedef uint8_t MacAddr[6];

  struct Target { String ssid = "-"; MacAddr bssid = {0, 0, 0, 0, 0, 0}; int channel = 1; };
  struct ApEntry { uint8_t bssid[6]; uint8_t channel; bool valid; };

  enum State { STATE_MAIN, STATE_SELECT_WIFI, STATE_RUNNING };
  enum Mode  { MODE_TARGET, MODE_ALL };

  State   _state       = STATE_MAIN;
  Mode    _mode        = MODE_TARGET;
  Target  _target;
  uint8_t _durIdx      = 1;    // index into kDur / kDurName
  uint8_t _mainSel     = 0;
  bool    _fromContext = false;
  bool    _chromeDrawn = false;
  uint8_t _allChanHop  = 0;

  class WifiAttackUtil* _attacker = nullptr;

  static constexpr int MAX_SCAN = 20;
  static constexpr int MAX_ALL  = 30;

  ListItem _mainItems[4];
  String   _modeSub;
  String   _targetSub;
  String   _durSub;

  ListItem _scanItems[MAX_SCAN + 1];
  char     _scanLabels[MAX_SCAN][52];
  char     _scanValues[MAX_SCAN][18];
  char     _scanDisplay[MAX_SCAN][24];
  int16_t  _scanRssi[MAX_SCAN]     = {};
  uint8_t  _scanChannels[MAX_SCAN] = {};
  int      _scanCount = 0;
  bool     _scanValid = false;

  static ApEntry      _allTargets[MAX_ALL];
  static int          _allCount;
  static portMUX_TYPE _allLock;
  static void _beaconCb(void* buf, wifi_promiscuous_pkt_type_t type);

  String _statusTitle;
  String _statusDetail;

  void _showMain();
  void _selectWifi(bool forceScan = false);
  void _showScanResults();
  void _startAttack();
  void _stopAttack();
  void _quietAll();
  void _drawStatus(const char* titleLine, const char* detailLine);
};
