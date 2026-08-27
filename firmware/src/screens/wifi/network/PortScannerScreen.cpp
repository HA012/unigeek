#include "PortScannerScreen.h"
#include <WiFi.h>
#include <ctype.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

void PortScannerScreen::onInit() {
  memset(_hosts,       0, sizeof(_hosts));
  memset(_results,     0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));
  memset(_configItems, 0, sizeof(_configItems));
  _showInput();
}

void PortScannerScreen::onBack() {
  if (_state == STATE_RESULTS) {
    _showInput();
  } else if (_state != STATE_SCANNING) {
    Screen.goBack();
  }
}

void PortScannerScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_INPUT || index >= _configCount) return;

  switch (_configActions[index]) {
    case CFG_MODE:
      _scanMode = (_scanMode == MODE_TARGET) ? MODE_RANGE : MODE_TARGET;
      _showInput();
      break;

    case CFG_TARGET_IP: {
      String initial = _targetIp.length() > 0 ? _targetIp : _networkPrefix();
      String ip = InputTextAction::popup("Target IP", initial.c_str(), InputTextAction::INPUT_IP_ADDRESS);
      if (!InputTextAction::wasCancelled()) _targetIp = ip;
      _showInput();
      break;
    }

    case CFG_START_IP:
      _startIp = InputNumberAction::popup("Start IP", 1, _endIp, _startIp);
      _showInput();
      break;

    case CFG_END_IP:
      _endIp = InputNumberAction::popup("End IP", _startIp, 254, _endIp);
      _showInput();
      break;

    case CFG_PORTS:
      _portMode = (PortMode)((_portMode + 1) % 3);
      _showInput();
      break;

    case CFG_START_PORT:
      _startPort = InputNumberAction::popup("Start Port", 1, _endPort, _startPort);
      _showInput();
      break;

    case CFG_END_PORT:
      _endPort = InputNumberAction::popup("End Port", _startPort, 65535, _endPort);
      _showInput();
      break;

    case CFG_CUSTOM_PORTS: {
      String ports = InputNumberAction::popupList("Custom Ports", _customPorts.c_str());
      if (!InputNumberAction::wasCancelled()) _customPorts = ports;
      _showInput();
      break;
    }

    case CFG_SERVICE_SCAN:
      _serviceScan = !_serviceScan;
      _showInput();
      break;

    case CFG_START_SCAN:
      _scan();
      break;
  }
}

// ── private ──────────────────────────────────────────────────────────────────

void PortScannerScreen::_showInput() {
  _state = STATE_INPUT;
  _configCount = 0;

  auto add = [&](const char* label, const char* sub, ConfigAction action) {
    _configItems[_configCount] = {label, sub};
    _configActions[_configCount] = action;
    _configCount++;
  };

  add("Mode", _scanMode == MODE_TARGET ? "Target" : "Range", CFG_MODE);

  if (_scanMode == MODE_TARGET) {
    _targetIpSub = _targetIp.length() > 0 ? _targetIp : "-";
    add("Target IP", _targetIpSub.c_str(), CFG_TARGET_IP);
  } else {
    _startIpSub = String(_startIp);
    _endIpSub   = String(_endIp);
    add("Start IP", _startIpSub.c_str(), CFG_START_IP);
    add("End IP",   _endIpSub.c_str(),   CFG_END_IP);
  }

  const char* portMode = _portMode == PORTS_COMMON ? "Common" :
                         _portMode == PORTS_RANGE  ? "Range"  : "Custom";
  add("Ports", portMode, CFG_PORTS);

  if (_portMode == PORTS_RANGE) {
    _startPortSub = String(_startPort);
    _endPortSub   = String(_endPort);
    add("Start Port", _startPortSub.c_str(), CFG_START_PORT);
    add("End Port",   _endPortSub.c_str(),   CFG_END_PORT);
  } else if (_portMode == PORTS_CUSTOM) {
    _customPortsSub = _customPorts.length() > 0 ? _customPorts : "-";
    add("Custom Ports", _customPortsSub.c_str(), CFG_CUSTOM_PORTS);
  }

  add("Service Scan", _serviceScan ? "On" : "Off", CFG_SERVICE_SCAN);
  add("Start Scan", nullptr, CFG_START_SCAN);
  setItems(_configItems, _configCount);
}

void PortScannerScreen::_scan() {
  if (WiFi.localIP()[0] == 0 && WiFi.localIP()[1] == 0 &&
      WiFi.localIP()[2] == 0 && WiFi.localIP()[3] == 0) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  if (_scanMode == MODE_TARGET) {
    if (_targetIp.length() == 0) {
      ShowStatusAction::show("Enter target IP first");
      return;
    }

    int a, b, c, d;
    if (sscanf(_targetIp.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4 ||
        a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) {
      ShowStatusAction::show("Invalid IP address");
      return;
    }
  }

  if (_portMode == PORTS_CUSTOM) {
    uint16_t ports[PortScanUtil::MAX_CUSTOM_PORTS];
    uint8_t count = 0;
    if (!_parseCustomPorts(ports, count)) {
      ShowStatusAction::show("Invalid custom ports");
      return;
    }
  }

  _state = STATE_SCANNING;
  memset(_hosts,       0, sizeof(_hosts));
  memset(_results,     0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));
  _resultCount = 0;

  int nps = Achievement.inc("wifi_port_scan_started");
  if (nps == 1) Achievement.unlock("wifi_port_scan_started");

  ProgressView::init();

  if (_scanMode == MODE_TARGET) {
    _scanTarget(_targetIp.c_str(), _resultCount);
  } else {
    uint8_t hostCount = IpScanUtil::scan(
      (uint8_t)_startIp, (uint8_t)_endIp,
      _hosts, MAX_FOUND_HOSTS, false,
      [](uint8_t pct) { ProgressView::progress("Finding hosts...", pct); }
    );

    for (uint8_t i = 0; i < hostCount && _resultCount < PortScanUtil::MAX_RESULTS; i++) {
      char msg[32];
      snprintf(msg, sizeof(msg), "Scanning %s", _hosts[i].ip);
      ProgressView::progress(msg, 0);
      _scanTarget(_hosts[i].ip, _resultCount);
    }
  }

  ProgressView::finish();

  if (_resultCount == 0) {
    _resultItems[0] = {"No ports open"};
    _state = STATE_RESULTS;
    setItems(_resultItems, 1);
    return;
  }

  int npo = Achievement.inc("wifi_port_open_found");
  if (npo == 1) Achievement.unlock("wifi_port_open_found");

  for (uint8_t i = 0; i < _resultCount; i++)
    _resultItems[i] = {_results[i].label, _results[i].service};

  _state = STATE_RESULTS;
  setItems(_resultItems, _resultCount);
}

bool PortScannerScreen::_scanTarget(const char* ip, uint8_t& count) {
  if (!ip || count >= PortScanUtil::MAX_RESULTS) return false;

  uint8_t remaining = PortScanUtil::MAX_RESULTS - count;
  uint8_t found = 0;

  if (_portMode == PORTS_COMMON) {
    found = PortScanUtil::scan(ip, &_results[count], remaining, "Port scanning...", _serviceScan);
  } else if (_portMode == PORTS_RANGE) {
    found = PortScanUtil::scanRange(ip, (uint16_t)_startPort, (uint16_t)_endPort,
                                    &_results[count], remaining, "Port scanning...", _serviceScan);
  } else {
    uint16_t ports[PortScanUtil::MAX_CUSTOM_PORTS];
    uint8_t portCount = 0;
    if (!_parseCustomPorts(ports, portCount)) return false;
    found = PortScanUtil::scanPorts(ip, ports, portCount,
                                    &_results[count], remaining, "Port scanning...", _serviceScan);
  }

  count += found;
  return true;
}

bool PortScannerScreen::_parseCustomPorts(uint16_t out[], uint8_t& count) {
  count = 0;
  if (!out) return false;

  String src = _customPorts;
  src.trim();
  if (src.length() == 0) return false;

  int pos = 0;
  while (pos < (int)src.length()) {
    while (pos < (int)src.length() && isspace((unsigned char)src[pos])) pos++;
    if (pos >= (int)src.length()) break;

    int start = pos;
    while (pos < (int)src.length() && !isspace((unsigned char)src[pos])) pos++;
    String token = src.substring(start, pos);
    for (size_t i = 0; i < token.length(); i++) {
      if (!isdigit((unsigned char)token[i])) return false;
    }

    long value = token.toInt();
    if (value < 1 || value > 65535) return false;

    bool duplicate = false;
    for (uint8_t i = 0; i < count; i++) {
      if (out[i] == (uint16_t)value) { duplicate = true; break; }
    }
    if (!duplicate) {
      if (count >= PortScanUtil::MAX_CUSTOM_PORTS) return false;
      out[count++] = (uint16_t)value;
    }
  }

  return count > 0;
}

String PortScannerScreen::_networkPrefix() const {
  IPAddress ip = WiFi.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) return "";
  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%u.%u.%u.", ip[0], ip[1], ip[2]);
  return String(prefix);
}
