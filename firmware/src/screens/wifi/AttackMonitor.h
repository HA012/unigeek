#pragma once

#include <cstdint>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include "ui/views/ScrollListView.h"
#include "services/VendorResolver.h"

// ── AttackMonitor ───────────────────────────────────────────────────────────
// Self-contained running view + live feedback for the Target-mode injection
// attacks (Deauth, CSA Spoof, Quiet DoS). Runs a promiscuous RX callback on the
// target channel alongside the injection and keeps a per-client table of the
// clear-text evidence (deauth/disassoc, auth/assoc requests, assoc responses,
// data frames). From that it infers a per-client state and drives a live list
// harmonized with the Packet Analyzer: MAC left, state right, sorted by frames
// seen; "*" marks the explicitly selected targets. Press freezes the preview
// (the attack and the capture keep running), Press again opens a live details
// view, Back resumes. There is no delivery ACK from injection, so this is
// inferred effect, not confirmation. Target mode only.
class AttackMonitor
{
public:
  enum Kind  { KIND_DEAUTH, KIND_CSA, KIND_QUIET };
  enum St    { ST_UNKNOWN, ST_CONNECTED, ST_RECONNECTING, ST_KICKED, ST_DOWN };

  struct Client {
    uint8_t  mac[6];
    int8_t   rssi;
    uint32_t packets;
    uint32_t cDeauth, cReassoc, cAssocOk, cData;
    uint32_t tDeauth, tReassoc, tAssocOk, tData, tAny;
  };

  void begin(Kind kind, const uint8_t ap[6],
             const uint8_t (*selected)[6], uint8_t selCount);
  void end();
  bool active() const { return _active; }

  void addSent(uint32_t n) { _sent += n; }
  void poll();                           // refresh counters + live details

  // Running-view input. onBack() returns false only on a live-list Back, which
  // the screen turns into "stop attack"; every other case is consumed here.
  void onScroll(int delta);
  void onPress();
  bool onBack();

  void draw(int x, int y, int w, int h);

private:
  enum View { VIEW_LIVE, VIEW_PAUSED, VIEW_DETAILS };

  static constexpr uint8_t MAX_CLIENTS = 24;
  static constexpr uint8_t MAX_ROWS    = 9;

  bool  _active = false;
  Kind  _kind   = KIND_DEAUTH;
  View  _view   = VIEW_LIVE;
  uint8_t _lastPainted = 255;            // force a body clear on view change
  uint32_t _lastDraw = 0;                // redraw throttle (decoupled from inject)
  bool     _dirty    = true;             // force an immediate redraw (input/begin)

  // ── ISR-side table (one monitor active at a time) ──
  static AttackMonitor* _self;
  static uint8_t        _apS[6];
  static portMUX_TYPE   _mux;
  static Client         _tbl[MAX_CLIENTS];
  static volatile uint8_t _count;
  static void _cb(void* buf, wifi_promiscuous_pkt_type_t type);
  static int  _find(const uint8_t* mac);     // caller holds _mux

  // Explicitly selected targets (for the "*" marker).
  uint8_t _selMacs[4][6] = {};
  uint8_t _selN = 0;
  bool    _isSelected(const uint8_t* mac) const;

  // ── Main-thread view state ──
  Client   _snap[MAX_CLIENTS];               // frozen list while paused
  St       _snapState[MAX_CLIENTS];          // state frozen at pause (truly static)
  uint8_t  _order[MAX_CLIENTS] = {};         // frozen display order (packets desc)
  uint8_t  _snapCount = 0;
  int      _scroll = 0;                      // live-list top row
  int      _sel    = 0;                      // paused selection (row position)

  uint32_t _sent = 0;

  // ── Details view ──
  ScrollListView::Row _rows[MAX_ROWS] = {};
  ScrollListView      _detailList;
  uint8_t  _detailMac[6] = {};
  bool     _lookupVendor = false;

  int   _snapshotLive(Client* out) const;    // copy+sort live table (packets desc)
  void  _enterDetails();
  void  _fillDetailRows(const Client& c);
  St    _state(const Client& c) const;
  const char* _stateName(St s) const;
  uint16_t    _stateColor(St s) const;
  uint8_t _metricCount(const Client* arr, int n) const;  // Kicked / Active
  void _drawList(int x, int y, int w, int h);
  void _drawDetails(int x, int y, int w, int h);
  void _drawFooter(int x, int y, int w, int h, int clients, int metricN);
};
