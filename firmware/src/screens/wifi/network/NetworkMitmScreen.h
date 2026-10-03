//
// NetworkMitmScreen — man-in-the-middle against the network you are joined to.
//
// Three toggles feeding one engine:
//
//   ARP Spoof       sweeps the whole subnet and poisons every host it finds,
//                   in both directions, so their traffic reaches us
//   Network Sniffer writes the traffic to a PCAP on the SD card
//
// DNS spoofing used to be a third toggle here and was removed. Serving the
// forged pages needs a web server, and on a board without PSRAM that server
// drove free heap to 1 KB — the WiFi driver takes its TX buffers from the same
// heap, so it stopped transmitting, the ARP poison stopped being refreshed and
// the victim was back on the real gateway within seconds. Use the AP-mode DNS
// spoof in WifiAPScreen instead: no relay, so no competition for the heap.
//
// The relay does the forwarding itself because CONFIG_LWIP_IP_FORWARD is
// compiled out of the Arduino lwIP — without it the ARP spoof would just cut
// the victims off. See MitmRelay.
//
// With ARP Spoof off the sniffer falls back to 802.11 promiscuous capture of
// the current channel: everything on the air, but CCMP-encrypted.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/LogView.h"
#include "utils/network/ArpSpoofer.h"
#include "utils/network/MitmRelay.h"
#include "utils/network/PcapWriter.h"

class NetworkMitmScreen : public ListScreen {
public:
  const char* title()    override { return "MITM Attack"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  ~NetworkMitmScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State  { STATE_MENU, STATE_RUNNING };
  enum Phase  { PHASE_SWEEP, PHASE_POISON };

  static constexpr const char* SAVE_DIR = "/unigeek/wifi/sniffer";

  State _state = STATE_MENU;
  Phase _phase = PHASE_SWEEP;

  // Options
  bool _arpEnabled     = false;
  bool _snifferEnabled = false;

  String _arpSub, _snifferSub;
  ListItem _menuItems[3];

  // Engine
  ArpSpoofer _arp;
  MitmRelay  _relay;
  PcapWriter _pcap;

  bool _relayUp = false;
  bool _arpUp   = false;
  bool _pcapUp  = false;

  unsigned long _lastSweep   = 0;
  unsigned long _lastPoison  = 0;
  unsigned long _lastRescan  = 0;
  unsigned long _lastStats   = 0;
  unsigned long _lastDraw    = 0;
  unsigned long _lastFreeChk = 0;
  int           _lastTargets   = 0;
  uint32_t      _lastProbeLost    = 0;
  uint32_t      _lastProbeDropped = 0;
  bool          _gwLogged      = false; // gateway MAC printed once, when learned

  LogView _log;

  uint8_t _menuIndex = 0;

  void _showMenu();
  void _start();
  void _stop(const char* reason);
  void _drawLog();
  bool _checkFreeSpace();
  static void _fmtMac(const uint8_t* mac, char* out, size_t n);
};
