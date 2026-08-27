#include "IPScannerScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

// ── screen methods ─────────────────────────────────────

IPScannerScreen::IPScannerScreen() {
  memset(_foundIPs,    0, sizeof(_foundIPs));
  memset(_foundItems,  0, sizeof(_foundItems));
  memset(_openPorts,   0, sizeof(_openPorts));
  memset(_openItems,   0, sizeof(_openItems));
  memset(_configItems, 0, sizeof(_configItems));
}

void IPScannerScreen::onInit() {
  _showConfiguration();
}

void IPScannerScreen::onBack() {
  switch (_state) {
    case STATE_RESULT_PORT:
      _state = STATE_RESULT_IP;
      setItems(_foundItems, (_foundCount == 0) ? 1 : _foundCount);
      break;
    case STATE_RESULT_IP:
      _showConfiguration();
      break;
    default:
      Screen.goBack();
      break;
  }
}

void IPScannerScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_CONFIGURATION) {
    uint8_t row = 0;

    if (index == row++) {
      _mode = (_mode == MODE_TARGET) ? MODE_RANGE : MODE_TARGET;
      _showConfiguration();
      return;
    }

    if (_mode == MODE_TARGET) {
      if (index == row++) {
        String ip = InputTextAction::popup("Target IP", _targetIp.c_str(), InputTextAction::INPUT_IP_ADDRESS);
        if (!InputTextAction::wasCancelled()) _targetIp = ip;
        _showConfiguration();
        return;
      }
    } else {
      if (index == row++) {
        int value = InputNumberAction::popup("Start IP", 1, _endIp, _startIp);
        if (!InputNumberAction::wasCancelled()) _startIp = value;
        _showConfiguration();
        return;
      }
      if (index == row++) {
        int value = InputNumberAction::popup("End IP", _startIp, 254, _endIp);
        if (!InputNumberAction::wasCancelled()) _endIp = value;
        _showConfiguration();
        return;
      }
    }

    if (index == row++) {
      _resolveName = !_resolveName;
      _showConfiguration();
      return;
    }

    if (index == row) {
      _scanIP();
      return;
    }
  } else if (_state == STATE_RESULT_IP) {
    if (_foundCount == 0 || _foundIPs[index].ip[0] == '\0') {
      _showConfiguration();
    } else {
      _scanPort(_foundIPs[index].ip);
    }
  }
}

// ── private ────────────────────────────────────────────

void IPScannerScreen::_showConfiguration() {
  _state = STATE_CONFIGURATION;
  uint8_t row = 0;

  _configItems[row++] = {"Mode", _mode == MODE_TARGET ? "Target" : "Range"};

  if (_mode == MODE_TARGET) {
    _targetIpSub = _targetIp.length() > 0 ? _targetIp : "-";
    _configItems[row++] = {"Target IP", _targetIpSub.c_str()};
  } else {
    _startIpSub = String(_startIp);
    _endIpSub   = String(_endIp);
    _configItems[row++] = {"Start IP", _startIpSub.c_str()};
    _configItems[row++] = {"End IP",   _endIpSub.c_str()};
  }

  _configItems[row++] = {"Resolve Name", _resolveName ? "On" : "Off"};
  _configItems[row++] = {"Start Scan"};
  setItems(_configItems, row);
}

bool IPScannerScreen::_validTargetIp() const {
  if (_targetIp.length() == 0) return false;

  int a, b, c, d;
  char tail;
  return sscanf(_targetIp.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) == 4 &&
         a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
         c >= 0 && c <= 255 && d >= 0 && d <= 255;
}

void IPScannerScreen::_scanIP() {
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  if (_mode == MODE_TARGET) {
    if (_targetIp.length() == 0) {
      ShowStatusAction::show("Enter target IP first");
      return;
    }
    if (!_validTargetIp()) {
      ShowStatusAction::show("Invalid IP address");
      return;
    }
  }

  _state = STATE_SCANNING_IP;
  memset(_foundIPs,   0, sizeof(_foundIPs));
  memset(_foundItems, 0, sizeof(_foundItems));
  _foundCount = 0;

  int nip = Achievement.inc("wifi_ip_scan_started");
  if (nip == 1) Achievement.unlock("wifi_ip_scan_started");

  ProgressView::init();

  if (_mode == MODE_TARGET) {
    ProgressView::progress("IP scanning...", 0);
    if (IpScanUtil::scanTarget(_targetIp.c_str(), _foundIPs[0], _resolveName)) {
      _foundCount = 1;
    }
    ProgressView::progress("IP scanning...", 100);
  } else {
    _foundCount = IpScanUtil::scan(
      (uint8_t)_startIp, (uint8_t)_endIp,
      _foundIPs, MAX_FOUND, _resolveName,
      [](uint8_t pct) { ProgressView::progress("IP scanning...", pct); }
    );
  }

  ProgressView::finish();

  if (_foundCount == 0) {
    _foundItems[0] = {"No devices found"};
    _state = STATE_RESULT_IP;
    setItems(_foundItems, 1);
    return;
  }

  int nh = Achievement.inc("wifi_ip_host_found");
  if (nh == 1) Achievement.unlock("wifi_ip_host_found");

  for (uint8_t i = 0; i < _foundCount; i++) {
    _foundItems[i] = {_foundIPs[i].ip, _foundIPs[i].hostname};
  }

  _state = STATE_RESULT_IP;
  setItems(_foundItems, _foundCount);
}

void IPScannerScreen::_scanPort(const char* ip) {
  _state = STATE_SCANNING_PORT;

  memset(_openPorts, 0, sizeof(_openPorts));
  memset(_openItems, 0, sizeof(_openItems));

  int nps = Achievement.inc("wifi_port_scan_started");
  if (nps == 1) Achievement.unlock("wifi_port_scan_started");

  ProgressView::init();
  _openCount = PortScanUtil::scan(ip, _openPorts, PortScanUtil::MAX_RESULTS);
  ProgressView::finish();

  if (_openCount == 0) {
    _openItems[0] = {"No ports open"};
    _state = STATE_RESULT_PORT;
    setItems(_openItems, 1);
    return;
  }

  int npo = Achievement.inc("wifi_port_open_found");
  if (npo == 1) Achievement.unlock("wifi_port_open_found");

  for (uint8_t i = 0; i < _openCount; i++) {
    _openItems[i] = {_openPorts[i].label, _openPorts[i].service};
  }

  _state = STATE_RESULT_PORT;
  setItems(_openItems, _openCount);
}
