#pragma once

#include "ui/templates/ListScreen.h"

// ── CSA Spoof (802.11h Channel Switch Announcement DoS) ─────────────────────
// Beacons spoofing a target AP that carry a Channel Switch Announcement element,
// so clients honouring 802.11h follow the "AP" to a channel where it is not and
// drop the link. Structured after WifiDeauthDisassocScreen (lean variant: no
// per-client targeting, no All mode — CSA is broadcast to the whole BSS).
class WifiCsaSpoofScreen : public ListScreen
{
public:
  const char* title() override { return "CSA Spoof"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  WifiCsaSpoofScreen() { memset(_mainItems, 0, sizeof(_mainItems)); }
  ~WifiCsaSpoofScreen() override;

  // Contextual entry from WiFi Scanner / Traffic Monitor: prefills the AP and
  // auto-starts in onInit.
  void configureFromChannel(uint8_t channel, const uint8_t* bssid, const char* ssid = nullptr);

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  struct Target { String ssid = "-"; uint8_t bssid[6] = {0, 0, 0, 0, 0, 0}; int channel = 1; };
  enum State { STATE_MAIN, STATE_SELECT_WIFI, STATE_RUNNING };

  State   _state       = STATE_MAIN;
  Target  _target;
  uint8_t _toChannel   = 11;   // channel the clients are told to move to
  uint8_t _mainSel     = 0;
  bool    _fromContext = false;
  bool    _chromeDrawn = false;

  class WifiAttackUtil* _attacker = nullptr;

  static constexpr int MAX_SCAN = 20;
  ListItem _mainItems[3];
  String   _targetSub;
  String   _toChannelSub;

  ListItem _scanItems[MAX_SCAN + 1];
  char     _scanLabels[MAX_SCAN][52];
  char     _scanValues[MAX_SCAN][18];
  char     _scanDisplay[MAX_SCAN][24];
  int16_t  _scanRssi[MAX_SCAN]     = {};
  uint8_t  _scanChannels[MAX_SCAN] = {};
  int      _scanCount = 0;
  bool     _scanValid = false;

  String _statusTitle;
  String _statusDetail;

  void _showMain();
  void _selectWifi(bool forceScan = false);
  void _showScanResults();
  void _defaultToChannel();
  void _startAttack();
  void _stopAttack();
  void _drawStatus(const char* titleLine, const char* detailLine);
};
