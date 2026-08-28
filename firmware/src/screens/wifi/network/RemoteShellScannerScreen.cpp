#include "RemoteShellScannerScreen.h"
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"
#include "screens/wifi/network/remote/SshClientScreen.h"
#include "screens/wifi/network/remote/TelnetClientScreen.h"

void RemoteShellScannerScreen::onInit()
{
  memset(_hosts, 0, sizeof(_hosts));
  memset(_results, 0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));
  memset(_configItems, 0, sizeof(_configItems));
  _showConfig();
}

void RemoteShellScannerScreen::onBack()
{
  if (_state == STATE_DETAILS) {
    _showResults();
    return;
  }

  if (_state == STATE_RESULTS) {
    _showConfig();
    return;
  }

  if (_state != STATE_SCANNING) {
    Screen.goBack();
  }
}

void RemoteShellScannerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_CONFIG) {
    if (index >= _configCount) return;

    switch (_configActions[index]) {
      case CFG_MODE:
        _scanMode = (_scanMode == MODE_RANGE) ? MODE_TARGETS : MODE_RANGE;
        _showConfig(index);
        break;

      case CFG_TARGET_1:
        _editTarget(0, index);
        break;
      case CFG_TARGET_2:
        _editTarget(1, index);
        break;
      case CFG_TARGET_3:
        _editTarget(2, index);
        break;
      case CFG_TARGET_4:
        _editTarget(3, index);
        break;

      case CFG_START_IP:
        _startIp = InputNumberAction::popup(
          "Start IP",
          1,
          _endIp,
          _startIp
        );
        _showConfig(index);
        break;

      case CFG_END_IP:
        _endIp = InputNumberAction::popup(
          "End IP",
          _startIp,
          254,
          _endIp
        );
        _showConfig(index);
        break;

      case CFG_START_SCAN:
        _scan();
        break;
    }
    return;
  }

  if (_state == STATE_RESULTS && index < _resultCount) {
    _showDetails(index);
    return;
  }

  if (_state == STATE_DETAILS &&
      _detailResultIndex < _resultCount &&
      index == DETAIL_CONNECT_ROW) {
    const auto& result = _results[_detailResultIndex];

    if (result.protocol == RemoteShellScanUtil::PROTO_SSH) {
      Screen.push(new SshClientScreen(result.ip, result.port));
    } else if (result.protocol == RemoteShellScanUtil::PROTO_TELNET) {
      Screen.push(new TelnetClientScreen(result.ip, result.port));
    }
  }
}

void RemoteShellScannerScreen::_showConfig(uint8_t selectedIndex)
{
  _state = STATE_CONFIG;
  _configCount = 0;

  auto add = [&](const char* label, const char* sub, ConfigAction action) {
    _configItems[_configCount] = {label, sub};
    _configActions[_configCount] = action;
    _configCount++;
  };

  add("Mode", _scanMode == MODE_RANGE ? "Range" : "Targets", CFG_MODE);

  if (_scanMode == MODE_TARGETS) {
    static const ConfigAction targetActions[MAX_TARGETS] = {
      CFG_TARGET_1,
      CFG_TARGET_2,
      CFG_TARGET_3,
      CFG_TARGET_4,
    };

    static const char* targetLabels[MAX_TARGETS] = {
      "IP 1",
      "IP 2",
      "IP 3",
      "IP 4",
    };

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      _targetSubs[i] = _targets[i].length() > 0 ? _targets[i] : "-";
      add(targetLabels[i], _targetSubs[i].c_str(), targetActions[i]);
    }
  } else {
    _startIpSub = String(_startIp);
    _endIpSub = String(_endIp);
    add("Start IP", _startIpSub.c_str(), CFG_START_IP);
    add("End IP", _endIpSub.c_str(), CFG_END_IP);
  }

  add("Start Scan", nullptr, CFG_START_SCAN);
  setItems(_configItems, _configCount, selectedIndex);
}

void RemoteShellScannerScreen::_scan()
{
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi");
    return;
  }

  if (_scanMode == MODE_TARGETS) {
    if (!_hasTargets()) {
      ShowStatusAction::show("Enter at least one IP");
      return;
    }

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (_targets[i].length() > 0 && !_validIp(_targets[i])) {
        ShowStatusAction::show("Invalid IP address");
        return;
      }
    }
  }

  _state = STATE_SCANNING;
  _resultCount = 0;
  memset(_results, 0, sizeof(_results));

  render();
  ScanCancelUtil::begin();
  ProgressView::init();

  if (_scanMode == MODE_TARGETS) {
    uint8_t targetCount = 0;
    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (_targets[i].length() > 0) targetCount++;
    }

    uint8_t done = 0;
    for (uint8_t i = 0;
         i < MAX_TARGETS && _resultCount < RemoteShellScanUtil::MAX_RESULTS;
         ++i) {
      if (_targets[i].length() == 0) continue;
      if (ScanCancelUtil::poll()) break;

      char label[48];
      snprintf(
        label, sizeof(label),
        "Scanning %s (%u/%u)...",
        _targets[i].c_str(),
        (unsigned)(done + 1),
        (unsigned)targetCount
      );
      ProgressView::progress(label, 0);
      _scanTarget(_targets[i].c_str());
      done++;

      ProgressView::progress(
        label,
        targetCount
          ? (uint8_t)((uint16_t)done * 100 / targetCount)
          : 100
      );
    }
  } else {
    _scanRange();
  }

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _showConfig();
    return;
  }
  _showResults();
}

void RemoteShellScannerScreen::_scanRange()
{
  uint8_t hostCount = IpScanUtil::scan(
    (uint8_t)_startIp,
    (uint8_t)_endIp,
    _hosts,
    MAX_FOUND_HOSTS,
    false,
    [](uint8_t pct) {
      ProgressView::progress(
        "Scanning hosts...",
        (uint8_t)((uint16_t)pct * 35 / 100)
      );
    },
    []() { return ScanCancelUtil::poll(); }
  );

  if (hostCount == 0) return;

  for (uint8_t i = 0;
       i < hostCount && _resultCount < RemoteShellScanUtil::MAX_RESULTS;
       ++i) {
    if (ScanCancelUtil::poll()) break;
    char label[48];
    snprintf(
      label, sizeof(label),
      "Scanning %s (%u/%u)...",
      _hosts[i].ip,
      (unsigned)(i + 1),
      (unsigned)hostCount
    );
    ProgressView::progress(label, 0);
    _scanTarget(_hosts[i].ip);

    ProgressView::progress(
      label,
      (uint8_t)(35 + ((uint16_t)(i + 1) * 65 / hostCount))
    );
  }
}

void RemoteShellScannerScreen::_scanTarget(const char* ip)
{
  RemoteShellScanUtil::Result result;
  const bool patient = _scanMode == MODE_TARGETS;

  if (_resultCount < RemoteShellScanUtil::MAX_RESULTS &&
      RemoteShellScanUtil::probeSsh(ip, result, patient)) {
    _results[_resultCount++] = result;
  }

  if (_resultCount < RemoteShellScanUtil::MAX_RESULTS &&
      RemoteShellScanUtil::probeTelnet(ip, result, patient)) {
    _results[_resultCount++] = result;
  }

  if (_resultCount < RemoteShellScanUtil::MAX_RESULTS &&
      RemoteShellScanUtil::probeWinRm(ip, false, result, patient)) {
    _results[_resultCount++] = result;
  }

  if (_resultCount < RemoteShellScanUtil::MAX_RESULTS &&
      RemoteShellScanUtil::probeWinRm(ip, true, result, patient)) {
    _results[_resultCount++] = result;
  }
}

void RemoteShellScannerScreen::_showResults()
{
  _state = STATE_RESULTS;

  if (_resultCount == 0) {
    _resultItems[0] = {"No remote shells found"};
    setItems(_resultItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _resultCount; ++i) {
    const auto& result = _results[i];

    _resultSubs[i] =
      String(_protocolName(result.protocol)) +
      " :" +
      String(result.port);

    _resultItems[i] = {
      result.ip,
      _resultSubs[i].c_str(),
    };
  }

  setItems(_resultItems, _resultCount);
}

void RemoteShellScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _resultCount) return;

  _state = STATE_DETAILS;
  _detailResultIndex = index;
  const auto& result = _results[index];

  _detailSubs[0] = result.ip;
  _detailItems[0] = {"IP", _detailSubs[0].c_str()};

  _detailSubs[1] = String(result.port);
  _detailItems[1] = {"Port", _detailSubs[1].c_str()};

  _detailSubs[2] = _protocolName(result.protocol);
  _detailItems[2] = {"Protocol", _detailSubs[2].c_str()};

  uint8_t row = 3;

  if (result.protocol == RemoteShellScanUtil::PROTO_WINRM_HTTP ||
      result.protocol == RemoteShellScanUtil::PROTO_WINRM_HTTPS) {
    _detailSubs[row] = result.status > 0 ? String(result.status) : "-";
    _detailItems[row] = {"Status", _detailSubs[row].c_str()};
    row++;
  }

  _detailSubs[row] = result.banner[0] ? result.banner : "-";
  _detailItems[row] = {"Banner", _detailSubs[row].c_str()};
  row++;

  if (result.protocol == RemoteShellScanUtil::PROTO_SSH ||
      result.protocol == RemoteShellScanUtil::PROTO_TELNET) {
    _detailItems[row] = {"Connect", nullptr};
    row++;
  }

  setItems(_detailItems, row);
}

void RemoteShellScannerScreen::_editTarget(
  uint8_t targetIndex,
  uint8_t selectedIndex
)
{
  if (targetIndex >= MAX_TARGETS) return;

  String initial = _targets[targetIndex].length() > 0
    ? _targets[targetIndex]
    : _networkPrefix();

  char title[8];
  snprintf(title, sizeof(title), "IP %u", (unsigned)(targetIndex + 1));

  String ip = InputTextAction::popup(
    title,
    initial.c_str(),
    InputTextAction::INPUT_IP_ADDRESS
  );

  if (!InputTextAction::wasCancelled()) {
    _targets[targetIndex] = ip;
  }

  _showConfig(selectedIndex);
}

bool RemoteShellScannerScreen::_validIp(const String& ip) const
{
  int a, b, c, d;
  char tail;

  return sscanf(
           ip.c_str(),
           "%d.%d.%d.%d%c",
           &a,
           &b,
           &c,
           &d,
           &tail
         ) == 4 &&
         a >= 0 && a <= 255 &&
         b >= 0 && b <= 255 &&
         c >= 0 && c <= 255 &&
         d >= 0 && d <= 255;
}

bool RemoteShellScannerScreen::_hasTargets() const
{
  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (_targets[i].length() > 0) return true;
  }

  return false;
}

String RemoteShellScannerScreen::_networkPrefix() const
{
  IPAddress ip = WiFi.localIP();

  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    return "";
  }

  char prefix[16];
  snprintf(
    prefix,
    sizeof(prefix),
    "%u.%u.%u.",
    ip[0],
    ip[1],
    ip[2]
  );

  return String(prefix);
}

const char* RemoteShellScannerScreen::_protocolName(
  RemoteShellScanUtil::Protocol protocol
) const
{
  switch (protocol) {
    case RemoteShellScanUtil::PROTO_SSH:
      return "SSH";
    case RemoteShellScanUtil::PROTO_TELNET:
      return "Telnet";
    case RemoteShellScanUtil::PROTO_WINRM_HTTP:
      return "WinRM";
    case RemoteShellScanUtil::PROTO_WINRM_HTTPS:
      return "WinRM TLS";
  }

  return "Unknown";
}
