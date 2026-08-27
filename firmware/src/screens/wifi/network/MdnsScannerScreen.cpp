#include "MdnsScannerScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

const char* MdnsScannerScreen::SERVICE_LABELS[SERVICE_COUNT] = {
  "All Services",
  "Printers",
  "Google Cast",
  "AirPlay",
  "SMB",
};

const char* MdnsScannerScreen::SERVICE_TYPES[SERVICE_COUNT] = {
  "_services._dns-sd._udp.local",
  "_ipp._tcp.local",
  "_googlecast._tcp.local",
  "_airplay._tcp.local",
  "_smb._tcp.local",
};

void MdnsScannerScreen::onInit()
{
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi", 1500);
    Screen.goBack();
    return;
  }

  memset(_results, 0, sizeof(_results));
  _showConfig();
}

void MdnsScannerScreen::onBack()
{
  if (_state == STATE_DETAILS) {
    _showResults();
    return;
  }

  if (_state == STATE_RESULTS) {
    _showConfig();
    return;
  }

  Screen.goBack();
}

void MdnsScannerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_CONFIG) {
    if (index == 0) {
      _serviceIndex = (_serviceIndex + 1) % SERVICE_COUNT;
      _showConfig();
    } else if (index == 1) {
      _scan();
    }
    return;
  }

  if (_state == STATE_RESULTS && index < _resultCount) {
    _showDetails(index);
  }
}

void MdnsScannerScreen::_showConfig()
{
  _state = STATE_CONFIG;
  _serviceSub = SERVICE_LABELS[_serviceIndex];

  _configItems[0] = {"Service", _serviceSub.c_str()};
  _configItems[1] = {"Start Scan"};
  setItems(_configItems, 2);
}

void MdnsScannerScreen::_scan()
{
  _state = STATE_SCANNING;
  _resultCount = 0;
  memset(_results, 0, sizeof(_results));

  ProgressView::init();

  if (_serviceIndex == 0) {
    // DNS-SD meta-query returns service types rather than instances. For the
    // first version, enumerate the service types already used elsewhere in the
    // firmware and merge their instances into one result list.
    for (uint8_t i = 1; i < SERVICE_COUNT; ++i) {
      if (_resultCount >= MdnsScanUtil::MAX_RESULTS) break;

      uint8_t remaining = MdnsScanUtil::MAX_RESULTS - _resultCount;
      ProgressView::progress(
        "Searching mDNS services...",
        (uint8_t)((i - 1) * 100 / (SERVICE_COUNT - 1))
      );
      uint8_t found = MdnsScanUtil::discover(
        SERVICE_TYPES[i],
        &_results[_resultCount],
        remaining,
        nullptr
      );
      _resultCount += found;
    }
  } else {
    _resultCount = MdnsScanUtil::discover(
      SERVICE_TYPES[_serviceIndex],
      _results,
      MdnsScanUtil::MAX_RESULTS,
      [](uint8_t pct) {
        ProgressView::progress("Searching mDNS services...", pct);
      }
    );
  }

  ProgressView::finish();

  if (_resultCount == 0) {
    ShowStatusAction::show("No mDNS services found", 1500);
    _showConfig();
    return;
  }

  _showResults();
}

void MdnsScannerScreen::_showResults()
{
  _state = STATE_RESULTS;

  for (uint8_t i = 0; i < _resultCount; ++i) {
    if (_results[i].ip[0]) {
      _resultSubs[i] = _results[i].ip;
    } else if (_results[i].host[0]) {
      _resultSubs[i] = _results[i].host;
    } else {
      _resultSubs[i] = _results[i].service;
    }

    _resultItems[i] = {
      _results[i].name[0] ? _results[i].name : "mDNS Service",
      _resultSubs[i].c_str()
    };
  }

  setItems(_resultItems, _resultCount);
}

void MdnsScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _resultCount) return;
  _state = STATE_DETAILS;

  const auto& svc = _results[index];

  _detailSubs[0] = svc.name[0]    ? svc.name    : "-";
  _detailSubs[1] = svc.host[0]    ? svc.host    : "-";
  _detailSubs[2] = svc.ip[0]      ? svc.ip      : "-";
  _detailSubs[3] = svc.service[0] ? svc.service : "-";
  _detailSubs[4] = String(svc.port);
  _detailSubs[5] = svc.txt[0]     ? svc.txt     : "-";

  _detailItems[0] = {"Name",    _detailSubs[0].c_str()};
  _detailItems[1] = {"Host",    _detailSubs[1].c_str()};
  _detailItems[2] = {"IP",      _detailSubs[2].c_str()};
  _detailItems[3] = {"Service", _detailSubs[3].c_str()};
  _detailItems[4] = {"Port",    _detailSubs[4].c_str()};
  _detailItems[5] = {"TXT",     _detailSubs[5].c_str()};

  setItems(_detailItems, DETAIL_ROWS);
}
