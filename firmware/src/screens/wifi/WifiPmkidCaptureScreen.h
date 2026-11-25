#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"

// ── PMKID Capture (clientless WPA2) ─────────────────────────────────────────
// Interactive, targeted counterpart to the Unigotchi's opportunistic PMKID
// harvesting. Sends an association request with an RSN IE to a WPA2 AP so it
// volunteers an EAPOL M1 carrying a PMKID — no associated client needed — and
// saves the beacon + M1 to a PCAP in /unigeek/wifi/eapol (the same store EAPOL
// Capture and Unigotchi use, so EAPOL Brute Force sees the file). Mirrors the
// attack screens' Mode Target/All; the active probe + PMKID detection come from
// PmkidUtil (shared with Unigotchi).
class WifiPmkidCaptureScreen : public ListScreen
{
public:
  const char* title() override { return "PMKID Capture"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  WifiPmkidCaptureScreen() {
    memset(_mainItems,  0, sizeof(_mainItems));
    memset(_scanItems,  0, sizeof(_scanItems));
    memset(_scanLabels,   0, sizeof(_scanLabels));
    memset(_scanValues,   0, sizeof(_scanValues));
    memset(_scanChannels, 0, sizeof(_scanChannels));
  }
  ~WifiPmkidCaptureScreen() override;

  void configureFromChannel(uint8_t channel, const uint8_t* bssid, const char* ssid = nullptr);

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  typedef uint8_t MacAddr[6];

  struct Target { String ssid = "-"; MacAddr bssid = {0, 0, 0, 0, 0, 0}; int channel = 1; };
  enum State { STATE_MAIN, STATE_SELECT_WIFI, STATE_RUNNING };
  enum Mode  { MODE_TARGET, MODE_ALL };

  State   _state       = STATE_MAIN;
  Mode    _mode        = MODE_TARGET;
  Target  _target;
  uint8_t _mainSel     = 0;
  bool    _fromContext = false;
  bool    _chromeDrawn = false;

  uint8_t       _spoofMac[6]  = {};
  uint8_t       _channel      = 1;
  uint8_t       _allChanHop   = 0;
  unsigned long _lastProbeMs  = 0;
  unsigned long _reprobeAt    = 0;
  uint32_t      _pmkids       = 0;

  class WifiAttackUtil* _attacker = nullptr;

  static const char* SAVE_DIR;

  // ── Discovered / targeted APs ────────────────────────────────────────────
  static constexpr int MAX_AP = 16;
  struct ApInfo {
    uint8_t  bssid[6];
    uint8_t  channel;
    char     ssid[33];
    bool     ssidKnown;
    bool     probed;
    bool     saved;
    uint16_t beaconLen;
    uint8_t  beacon[256];
  };
  ApInfo _aps[MAX_AP] = {};
  int    _apCount = 0;

  int  _findAp(const uint8_t* bssid) const;
  int  _ensureAp(const uint8_t* bssid, uint8_t ch);

  // ── ISR → loop ring (SPSC) ───────────────────────────────────────────────
  static constexpr int RING = 16;
  static constexpr int MAXF = 400;
  struct Raw { uint8_t data[MAXF]; uint16_t len; uint8_t channel; };
  static Raw            _ring[RING];
  static volatile int   _ringHead;
  static volatile int   _ringTail;
  static WifiPmkidCaptureScreen* _self;
  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

  void _processFrame(const uint8_t* data, uint16_t len, uint8_t ch);
  void _probe();
  void _saveCapture(ApInfo& ap, const uint8_t* m1, uint16_t m1Len);

  // ── Menu / scan (same pattern as the attack screens) ─────────────────────
  static constexpr int MAX_SCAN = 20;
  ListItem _mainItems[3];
  String   _modeSub;
  String   _targetSub;

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
  void _startCapture();
  void _stopCapture();
  void _drawStatus(const char* titleLine, const char* detailLine);
};
