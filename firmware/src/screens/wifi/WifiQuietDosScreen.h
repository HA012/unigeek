#pragma once

#include "ui/templates/ListScreen.h"

// ── Quiet DoS (802.11h Quiet element) ───────────────────────────────────────
// Beacons spoofing a target AP that carry a Quiet element asking stations to
// withhold transmissions for a chosen duration every beacon interval. Clients
// honouring 802.11h fall silent, degrading the BSS throughput. Same lean
// structure as WifiCsaSpoofScreen (AP target only, broadcast — no per-client).
class WifiQuietDosScreen : public ListScreen
{
public:
  const char* title() override { return "Quiet DoS"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  WifiQuietDosScreen() { memset(_mainItems, 0, sizeof(_mainItems)); }
  ~WifiQuietDosScreen() override;

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
  uint8_t _durIdx      = 1;    // index into kDur / kDurName
  uint8_t _mainSel     = 0;
  bool    _fromContext = false;
  bool    _chromeDrawn = false;

  class WifiAttackUtil* _attacker = nullptr;

  static constexpr int MAX_SCAN = 20;
  ListItem _mainItems[3];
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

  String _statusTitle;
  String _statusDetail;

  void _showMain();
  void _selectWifi(bool forceScan = false);
  void _showScanResults();
  void _startAttack();
  void _stopAttack();
  void _drawStatus(const char* titleLine, const char* detailLine);
};
