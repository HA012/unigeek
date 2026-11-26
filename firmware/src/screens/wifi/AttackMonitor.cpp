#include "AttackMonitor.h"
#include "core/Device.h"

#include <Arduino.h>
#include <cstring>

// ── Statics ─────────────────────────────────────────────────────────────────
AttackMonitor*  AttackMonitor::_self   = nullptr;
uint8_t         AttackMonitor::_apS[6] = {};
portMUX_TYPE    AttackMonitor::_mux    = portMUX_INITIALIZER_UNLOCKED;
AttackMonitor::Client AttackMonitor::_tbl[AttackMonitor::MAX_CLIENTS] = {};
volatile uint8_t AttackMonitor::_count = 0;

static inline bool macEq(const uint8_t* a, const uint8_t* b) {
  return a && b && memcmp(a, b, 6) == 0;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────
void AttackMonitor::begin(Kind kind, const uint8_t ap[6],
                          const uint8_t (*selected)[6], uint8_t selCount)
{
  _kind = kind;
  _view = VIEW_LIVE;
  _lastPainted = 255;
  _scroll = _sel = 0;
  _sent = 0;
  _lookupVendor = false;
  _lastDraw = 0;
  _dirty = true;

  _selN = selCount > 4 ? 4 : selCount;
  for (uint8_t i = 0; i < _selN; ++i) memcpy(_selMacs[i], selected[i], 6);

  memcpy(_apS, ap, 6);
  portENTER_CRITICAL(&_mux);
  memset(_tbl, 0, sizeof(_tbl));
  _count = 0;
  for (uint8_t i = 0; i < _selN; ++i) {        // seed selected targets so they
    memcpy(_tbl[_count].mac, _selMacs[i], 6);  // appear even while silent
    _count++;
  }
  portEXIT_CRITICAL(&_mux);

  _self = this;
  esp_wifi_set_promiscuous_rx_cb(&AttackMonitor::_cb);
  esp_wifi_set_promiscuous(true);
  _active = true;
}

void AttackMonitor::end()
{
  if (!_active) return;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  _self = nullptr;
  _active = false;
}

// ── RX callback — per-client clear-text evidence ────────────────────────────
int AttackMonitor::_find(const uint8_t* mac)
{
  for (uint8_t i = 0; i < _count; ++i)
    if (memcmp(_tbl[i].mac, mac, 6) == 0) return (int)i;
  return -1;
}

void AttackMonitor::_cb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (!_self || !buf) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* f = pkt->payload;
  const uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 16) return;

  const uint8_t ftype = (f[0] >> 2) & 0x03;
  const uint8_t sub   = (f[0] >> 4) & 0x0F;
  const uint8_t* a1 = f + 4;
  const uint8_t* a2 = f + 10;
  const uint8_t* a3 = (len >= 22) ? f + 16 : nullptr;

  // Must involve the target AP (BSSID can sit in any of the three addresses).
  if (!(macEq(a1, _apS) || macEq(a2, _apS) || (a3 && macEq(a3, _apS)))) return;

  // The station is the a1/a2 address that isn't the AP; unicast only.
  const uint8_t* sta = macEq(a1, _apS) ? a2 : a1;
  if (!sta || (sta[0] & 0x01) || macEq(sta, _apS)) return;
  // Targeted run: track only the explicitly selected clients.
  if (_self->_selN > 0 && !_self->_isSelected(sta)) return;

  const uint32_t now = millis();
  portENTER_CRITICAL(&_mux);
  int idx = _find(sta);
  if (idx < 0) {
    if (_count >= MAX_CLIENTS) { portEXIT_CRITICAL(&_mux); return; }
    idx = _count++;
    memset(&_tbl[idx], 0, sizeof(Client));
    memcpy(_tbl[idx].mac, sta, 6);
  }
  Client& c = _tbl[idx];
  c.rssi = pkt->rx_ctrl.rssi;
  c.packets++;
  c.tAny = now;
  if (ftype == 0) {                               // management (clear text)
    if (sub == 12 || sub == 10) { c.cDeauth++; c.tDeauth = now; }       // deauth/disassoc
    else if (sub == 11 || sub == 0 || sub == 2) { c.cReassoc++; c.tReassoc = now; } // auth/(re)assoc req
    else if (sub == 1 || sub == 3) {              // (re)assoc response → success?
      if (len >= 28) {
        const uint16_t st = (uint16_t)f[26] | ((uint16_t)f[27] << 8);
        if (st == 0) { c.cAssocOk++; c.tAssocOk = now; }
      }
    }
  } else if (ftype == 2) {                         // data
    // Only uplink data (client → AP, so the station is the transmitter) proves
    // the client is still on this channel. Downlink retries the AP keeps sending
    // to a client that already moved/left would otherwise read as "Active".
    if (macEq(a1, _apS)) {
      if (c.tData) {                                // learn the client's cadence
        uint32_t ivl = now - c.tData;
        if (ivl > 15000) ivl = 15000;              // bound a one-off long gap
        c.upIvl = c.upIvl ? (c.upIvl * 3 + ivl) / 4 : ivl;
      }
      c.cData++; c.tData = now;
    }
  }
  portEXIT_CRITICAL(&_mux);
}

// ── Per-client state inference ──────────────────────────────────────────────
AttackMonitor::St AttackMonitor::_state(const Client& c) const
{
  // Evidence is one-sided: we only hear this channel, so a silent uplink is
  // ambiguous between gone / idle / quieted. We lean on positive signals where
  // we have them (reassoc attempts for Deauth) and require a real activity
  // baseline before claiming an effect, to avoid single-frame flapping.
  const uint32_t now = millis();
  // Adaptive presence window: judge silence against the client's own uplink
  // cadence. A chatty client is called silent quickly (fast move/kick detection);
  // a sporadic one gets slack so idle isn't misread as gone. Clamped per kind.
  uint32_t upWin;
  if (c.upIvl == 0) {
    upWin = (_kind == KIND_DEAUTH) ? 3000 : 2500;
  } else {
    upWin = c.upIvl * 3;
    const uint32_t lo = (_kind == KIND_DEAUTH) ? 2000 : 1500;
    const uint32_t hi = (_kind == KIND_DEAUTH) ? 8000 : 6000;
    if (upWin < lo) upWin = lo;
    if (upWin > hi) upWin = hi;
  }
  const bool up = c.tData && (now - c.tData) < upWin;      // client transmitting now

  if (_kind == KIND_DEAUTH) {
    // Had a real association at some point: carried uplink, associated OK, or at
    // least attempted to (re)join.
    const bool everOn = c.cData >= 2 || c.cAssocOk >= 1 || c.cReassoc >= 1;
    const bool reconnecting = c.tReassoc && (now - c.tReassoc) < 4000;
    if (reconnecting) return ST_RECONNECTING;   // (re)joining = being kicked (strongest signal)
    if (up)           return ST_CONNECTED;       // transmitting normally
    if (everOn) {
      uint32_t last = c.tData;                   // most recent sign of life
      if (c.tReassoc > last) last = c.tReassoc;
      if (c.tAssocOk > last) last = c.tAssocOk;
      return (now - last) < 10000 ? ST_KICKED : ST_DOWN;
    }
    return ST_UNKNOWN;
  }

  // CSA / Quiet: success = the client stops transmitting (moved / silenced).
  // Presence is proven only by uplink; require a baseline so we don't flip a
  // client to "Moved" off one stray frame.
  const bool everOn = c.cData >= 2 || c.cAssocOk >= 1;
  if (up)     return ST_CONNECTED;               // "Active"
  if (everOn) return ST_DOWN;                     // was transmitting, now silent
  return ST_UNKNOWN;
}

// Hysteresis: the raw state flips on every stray frame, so the *displayed* state
// (c.stCur) only follows once the raw state has held long enough. Re-entering
// "Active"/"Connected" is the most damped, so a lone uplink frame can't undo a
// confirmed "Moved"/"Kicked"; leaving it (silence) needs sustained quiet too,
// while "Reconnecting" is immediate so a real kick shows at once.
void AttackMonitor::_commit(Client& c, uint32_t now)
{
  const St raw = _state(c);
  if (raw == (St)c.stCur) { c.stCandSince = 0; return; }       // already displayed
  if (c.stCandSince == 0 || raw != (St)c.stCand) {             // (new) candidate
    c.stCand = (uint8_t)raw;
    c.stCandSince = now ? now : 1;
    return;
  }
  uint32_t dwell;                                              // hold time before commit
  switch (raw) {
    case ST_CONNECTED:    dwell = 2000; break;  // hard to (re)enter Active → kills flicker
    case ST_DOWN:         dwell = 1500; break;  // sustained silence before Moved/Silent/Down
    case ST_KICKED:       dwell = 800;  break;
    case ST_RECONNECTING: dwell = 0;    break;  // show a real kick immediately
    default:              dwell = 500;  break;  // UNKNOWN
  }
  if (now - c.stCandSince >= dwell) { c.stCur = (uint8_t)raw; c.stCandSince = 0; }
}

const char* AttackMonitor::_stateName(St s) const
{
  if (_kind == KIND_DEAUTH) {
    switch (s) {
      case ST_CONNECTED:    return "Connected";
      case ST_RECONNECTING: return "Reconnecting";
      case ST_KICKED:       return "Kicked";
      case ST_DOWN:         return "Down";
      default:              return "-";
    }
  }
  // CSA / Quiet
  switch (s) {
    case ST_CONNECTED: return "Active";
    case ST_DOWN:      return (_kind == KIND_CSA) ? "Moved" : "Silent";
    default:           return "-";
  }
}

uint16_t AttackMonitor::_stateColor(St s) const
{
  switch (s) {
    case ST_KICKED:
    case ST_DOWN:         return TFT_GREEN;     // effect confirmed
    case ST_RECONNECTING: return TFT_YELLOW;    // in progress
    case ST_CONNECTED:    return TFT_WHITE;     // still up (attack not landing)
    default:              return TFT_DARKGREY;  // unknown
  }
}

bool AttackMonitor::_isSelected(const uint8_t* mac) const
{
  for (uint8_t i = 0; i < _selN; ++i)
    if (memcmp(_selMacs[i], mac, 6) == 0) return true;
  return false;
}

uint8_t AttackMonitor::_metricCount(const Client* arr, int n) const
{
  // Deauth → Kicked/Down; CSA/Quiet → Active (still transmitting).
  uint8_t m = 0;
  for (int i = 0; i < n; ++i) {
    const St s = (St)arr[i].stCur;               // committed (displayed) state
    if (_kind == KIND_DEAUTH) { if (s == ST_KICKED || s == ST_DOWN) ++m; }
    else                      { if (s == ST_CONNECTED) ++m; }
  }
  return m;
}

// ── Snapshot (copy + sort by frames seen, desc) ─────────────────────────────
int AttackMonitor::_snapshotLive(Client* out) const
{
  int n;
  portENTER_CRITICAL(&_mux);                      // hold the lock only for the copy
  n = _count;
  memcpy(out, (const void*)_tbl, n * sizeof(Client));
  portEXIT_CRITICAL(&_mux);
  for (int i = 1; i < n; ++i) {                   // insertion sort in place, packets desc
    Client cur = out[i];
    int j = i;
    while (j > 0 && out[j - 1].packets < cur.packets) { out[j] = out[j - 1]; --j; }
    out[j] = cur;
  }
  return n;
}

void AttackMonitor::poll()
{
  if (!_active) return;

  // Advance each client's hysteresis so the displayed state is stable. The table
  // only ever grows (no reorder/remove), so this runs under one brief lock.
  const uint32_t now = millis();
  portENTER_CRITICAL(&_mux);
  for (uint8_t i = 0; i < _count; ++i) _commit(_tbl[i], now);
  portEXIT_CRITICAL(&_mux);

  // Live details: refresh the open client's rows from the live table.
  if (_view == VIEW_DETAILS) {
    Client c;
    bool found = false;
    portENTER_CRITICAL(&_mux);
    int idx = _find(_detailMac);
    if (idx >= 0) { c = _tbl[idx]; found = true; }
    portEXIT_CRITICAL(&_mux);
    if (found) _fillDetailRows(c);
    if (_lookupVendor) {
      VendorResolver::Result r;
      if (VendorResolver::instance().poll(_detailMac, r)) {
        _rows[1].value = r.found ? String(r.name) : "Unknown";
        _lookupVendor = false;
      }
    }
  }
}

// ── Input ───────────────────────────────────────────────────────────────────
void AttackMonitor::onScroll(int delta)
{
  _dirty = true;
  if (_view == VIEW_LIVE) {
    _scroll += delta;
    if (_scroll < 0) _scroll = 0;                 // upper clamp done at draw
  } else if (_view == VIEW_PAUSED) {
    if (_snapCount == 0) return;
    _sel += delta;
    if (_sel < 0) _sel = _snapCount - 1;          // wrap, like the Packet Analyzer
    if (_sel >= _snapCount) _sel = 0;
  } else {                                        // VIEW_DETAILS
    _detailList.onNav(delta > 0 ? INavigation::DIR_DOWN : INavigation::DIR_UP);
  }
}

void AttackMonitor::onPress()
{
  _dirty = true;
  if (_view == VIEW_LIVE) {
    _snapCount = _snapshotLive(_snap);            // freeze preview (attack keeps running)
    for (uint8_t i = 0; i < _snapCount; ++i) { _order[i] = i; _snapState[i] = (St)_snap[i].stCur; }
    _sel = _scroll < _snapCount ? _scroll : 0;
    _view = VIEW_PAUSED;
  } else if (_view == VIEW_PAUSED) {
    if (_snapCount == 0) return;
    _enterDetails();
    _view = VIEW_DETAILS;
  }
}

bool AttackMonitor::onBack()
{
  _dirty = true;
  if (_view == VIEW_DETAILS) { _view = VIEW_PAUSED; _lastPainted = 255; return true; }
  if (_view == VIEW_PAUSED)  { _view = VIEW_LIVE;   _lastPainted = 255; return true; }
  return false;                                   // live Back → screen stops the attack
}

// ── Details rows (mirrors the Packet Analyzer client-details layout) ────────
void AttackMonitor::_enterDetails()
{
  const Client& c = _snap[_order[_sel]];
  memcpy(_detailMac, c.mac, 6);

  static const char* kLabels[MAX_ROWS] = {
    "MAC", "Vendor", "State", "RSSI", "Kicks", "Reconn", "Assoc OK", "Data", "Last"
  };
  for (uint8_t i = 0; i < MAX_ROWS; ++i) { _rows[i].label = kLabels[i]; _rows[i].secondary = nullptr; }

  if (_detailMac[0] & 0x02) { _rows[1].value = "Random address"; _lookupVendor = false; }
  else { _rows[1].value = "Looking up..."; _lookupVendor = VendorResolver::instance().request(_detailMac); }

  _fillDetailRows(c);
  _detailList.resetScroll();
  _detailList.setRows(_rows, MAX_ROWS);
}

void AttackMonitor::_fillDetailRows(const Client& c)
{
  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
  _rows[0].value = mac;
  // _rows[1] (Vendor) owned by the lookup path.
  _rows[2].value = _stateName((St)c.stCur);
  _rows[3].value = String((int)c.rssi) + " dBm";
  _rows[4].value = String(c.cDeauth);
  _rows[5].value = String(c.cReassoc);
  _rows[6].value = String(c.cAssocOk);
  _rows[7].value = String(c.cData);
  _rows[8].value = c.tAny ? (String((millis() - c.tAny) / 1000) + "s ago") : "-";
}

// ── Draw ────────────────────────────────────────────────────────────────────
void AttackMonitor::draw(int x, int y, int w, int h)
{
  if (!_active) return;
  // Decouple the redraw from the injection loop: refresh on input (_dirty) or
  // at most a few times a second, so the heavy list/sprite paint doesn't slow
  // the per-tick frame injection.
  const uint32_t now = millis();
  if (!_dirty) {
    if (_view == VIEW_PAUSED) return;             // paused is static: redraw on input only
    if ((now - _lastDraw) < 400) return;          // live/details refresh a few times/s
  }
  _lastDraw = now;
  _dirty = false;

  if (_view == VIEW_DETAILS) { _drawDetails(x, y, w, h); return; }
  _drawList(x, y, w, h);
}

void AttackMonitor::_drawList(int x, int y, int w, int h)
{
  auto& lcd = Uni.Lcd;
  const uint8_t viewId = (_view == VIEW_PAUSED) ? 1 : 0;
  if (_lastPainted != viewId) { lcd.fillRect(x, y, w, h, TFT_BLACK); _lastPainted = viewId; }

  const bool paused = _view == VIEW_PAUSED;
  const int footerH = 28, rowH = 19, top = y + 2;
  const int visible = (h - 2 - footerH) / rowH;

  // Live view reads the live table each frame; paused view uses the snapshot.
  Client live[MAX_CLIENTS];
  int liveN = 0;
  if (!paused) liveN = _snapshotLive(live);
  const int rows = paused ? _snapCount : liveN;

  if (visible > 0) {
    if (paused) {
      if (_sel < _scroll) _scroll = _sel;
      if (_sel >= _scroll + visible) _scroll = _sel - visible + 1;
    }
    const int maxScroll = rows > visible ? rows - visible : 0;
    if (_scroll > maxScroll) _scroll = maxScroll;
    if (_scroll < 0) _scroll = 0;
    const bool hasBar = rows > visible;
    const int rightEdge = x + w - (hasBar ? 7 : 3);

    lcd.setTextSize(1);
    int drawn = 0;
    for (int i = 0; i < visible && i + _scroll < rows; ++i) {
      const int pos = i + _scroll;
      const int yy = top + i * rowH;
      const int ci = paused ? _order[pos] : pos;
      const Client& c = paused ? _snap[ci] : live[ci];
      const bool selected = paused && pos == _sel;
      const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;

      lcd.fillRect(x, yy - 2, w - 3, rowH, TFT_BLACK);
      if (selected) lcd.fillRoundRect(x, yy - 2, rightEdge - x, rowH, 3, bg);

      char mac[20];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
      lcd.setTextDatum(TL_DATUM);
      lcd.setTextColor(TFT_WHITE, bg);
      lcd.drawString(mac, x + 3, yy);

      const St s = paused ? _snapState[ci] : (St)c.stCur;
      lcd.setTextDatum(TR_DATUM);
      lcd.setTextColor(selected ? TFT_WHITE : _stateColor(s), bg);
      lcd.drawString(_stateName(s), rightEdge - 3, yy);
      lcd.setTextDatum(TL_DATUM);
      ++drawn;
    }
    if (drawn < visible)
      lcd.fillRect(x, top + drawn * rowH, w - 3, (visible - drawn) * rowH, TFT_BLACK);

    // Scrollbar.
    const int trackH = visible * rowH;
    lcd.fillRect(x + w - 3, top, 3, trackH, 0x2104);
    if (rows > visible) {
      int sbH = max(4, trackH * visible / rows);
      if (sbH > trackH) sbH = trackH;
      int sbY = (trackH - sbH) * _scroll / max(1, rows - visible);
      lcd.fillRect(x + w - 3, top + sbY, 3, sbH, Config.getThemeColor());
    }

    if (rows == 0) {
      lcd.setTextDatum(MC_DATUM);
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      lcd.drawString("Listening...", x + w / 2, top + trackH / 2);
      lcd.setTextDatum(TL_DATUM);
    }
  }

  int metricN;
  if (paused) {                                   // count from the frozen states
    int m = 0;
    for (int i = 0; i < _snapCount; ++i) {
      const St s = _snapState[i];
      if (_kind == KIND_DEAUTH) { if (s == ST_KICKED || s == ST_DOWN) ++m; }
      else                      { if (s == ST_CONNECTED) ++m; }
    }
    metricN = m;
  } else {
    metricN = _metricCount(live, rows);
  }
  _drawFooter(x, y, w, h, rows, metricN);
}

void AttackMonitor::_drawFooter(int x, int y, int w, int h, int clients, int metricN)
{
  auto& lcd = Uni.Lcd;
  const bool paused = _view == VIEW_PAUSED;
  const int footerH = 28;
  const int footerTop = y + h - footerH;

  lcd.fillRect(x, footerTop, w, footerH, TFT_BLACK);
  lcd.drawFastHLine(x, footerTop, w, TFT_DARKGREY);

  // Upper bar — data. "Clients: X   Kicked/Active: Y".
  char data[40];
  const char* metric = (_kind == KIND_DEAUTH) ? "Kicked" : "Active";
  snprintf(data, sizeof(data), "Clients: %u   %s: %u",
           (unsigned)clients, metric, (unsigned)metricN);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString(data, x + w / 2, footerTop + 7);

  // Separator between data and hints.
  lcd.drawFastHLine(x, footerTop + 14, w, TFT_DARKGREY);

  // Lower bar — hints.
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString(paused ? "[Press] Details   [Back] Resume"
                        : "[Press] Pause Preview",
                 x + w / 2, y + h - 1);
  lcd.setTextDatum(TL_DATUM);
}

void AttackMonitor::_drawDetails(int x, int y, int w, int h)
{
  auto& lcd = Uni.Lcd;
  if (_lastPainted != 2) { lcd.fillRect(x, y, w, h, TFT_BLACK); _lastPainted = 2; }

  const int footerH = 14;
  _detailList.render(x, y + 2, w, h - 2 - footerH);

  lcd.fillRect(x, y + h - footerH, w, footerH, TFT_BLACK);
  lcd.drawFastHLine(x, y + h - footerH, w, TFT_DARKGREY);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString("[Back] Return", x + w / 2, y + h - 1);
  lcd.setTextDatum(TL_DATUM);
}
