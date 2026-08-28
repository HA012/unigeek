#include "PrinterScannerScreen.h"
#include <WiFi.h>
#include <string.h>
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

void PrinterScannerScreen::onInit()
{
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi", 1500);
    Screen.goBack();
    return;
  }

  memset(_printers, 0, sizeof(_printers));
  _scan();
}

void PrinterScannerScreen::onBack()
{
  if (_state == STATE_DETAILS) {
    _showResults();
    return;
  }

  Screen.goBack();
}

void PrinterScannerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_RESULTS && index < _printerCount) {
    _showDetails(index);
  }
}

void PrinterScannerScreen::_scan()
{
  _state = STATE_SCANNING;
  _printerCount = 0;
  memset(_printers, 0, sizeof(_printers));

  render();
  ProgressView::init();

  static SsdpScanUtil::Device ssdp[SsdpScanUtil::MAX_DEVICES];
  memset(ssdp, 0, sizeof(ssdp));
  uint8_t ssdpCount = SsdpScanUtil::discover(
    "urn:schemas-upnp-org:device:Printer:1",
    ssdp,
    SsdpScanUtil::MAX_DEVICES,
    [](uint8_t pct) {
      ProgressView::progress("Scanning...", pct / 2);
    }
  );

  for (uint8_t i = 0; i < ssdpCount; ++i) {
    _mergeSsdp(ssdp[i]);
  }

  static MdnsScanUtil::Service mdns[MdnsScanUtil::MAX_RESULTS];
  memset(mdns, 0, sizeof(mdns));
  uint8_t mdnsCount = MdnsScanUtil::discover(
    "_ipp._tcp.local",
    mdns,
    MdnsScanUtil::MAX_RESULTS,
    [](uint8_t pct) {
      ProgressView::progress("Scanning...", 50 + pct / 2);
    }
  );

  for (uint8_t i = 0; i < mdnsCount; ++i) {
    _mergeMdns(mdns[i]);
  }

  ProgressView::finish();

  if (_printerCount == 0) {
    ShowStatusAction::show("No printers found", 1500);
    Screen.goBack();
    return;
  }

  _showResults();
}

void PrinterScannerScreen::_showResults()
{
  _state = STATE_RESULTS;

  for (uint8_t i = 0; i < _printerCount; ++i) {
    if (_printers[i].ip[0]) {
      _resultSubs[i] = _printers[i].ip;
    } else if (_printers[i].host[0]) {
      _resultSubs[i] = _printers[i].host;
    } else {
      _resultSubs[i] = _printers[i].source;
    }

    _resultItems[i] = {
      _printers[i].name[0] ? _printers[i].name : "Network Printer",
      _resultSubs[i].c_str()
    };
  }

  setItems(_resultItems, _printerCount);
}

void PrinterScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _printerCount) return;
  _state = STATE_DETAILS;

  const Printer& p = _printers[index];

  _detailSubs[0] = p.name[0]     ? p.name     : "-";
  _detailSubs[1] = p.ip[0]       ? p.ip       : "-";
  _detailSubs[2] = p.host[0]     ? p.host     : "-";
  _detailSubs[3] = p.source[0]   ? p.source   : "-";
  _detailSubs[4] = p.port ? String(p.port) : "-";
  _detailSubs[5] = p.service[0]  ? p.service  : "-";
  _detailSubs[6] = p.server[0]   ? p.server   : "-";
  _detailSubs[7] = p.location[0] ? p.location : "-";

  _detailItems[0] = {"Name",      _detailSubs[0].c_str()};
  _detailItems[1] = {"IP",        _detailSubs[1].c_str()};
  _detailItems[2] = {"Host",      _detailSubs[2].c_str()};
  _detailItems[3] = {"Discovery", _detailSubs[3].c_str()};
  _detailItems[4] = {"Port",      _detailSubs[4].c_str()};
  _detailItems[5] = {"Service",   _detailSubs[5].c_str()};
  _detailItems[6] = {"Server",    _detailSubs[6].c_str()};
  _detailItems[7] = {"Location",  _detailSubs[7].c_str()};

  setItems(_detailItems, DETAIL_ROWS);
}

PrinterScannerScreen::Printer* PrinterScannerScreen::_findByIp(const char* ip)
{
  if (!ip || !*ip) return nullptr;

  for (uint8_t i = 0; i < _printerCount; ++i) {
    if (_printers[i].ip[0] && strcmp(_printers[i].ip, ip) == 0) {
      return &_printers[i];
    }
  }

  return nullptr;
}

PrinterScannerScreen::Printer* PrinterScannerScreen::_findByHost(const char* host)
{
  if (!host || !*host) return nullptr;

  for (uint8_t i = 0; i < _printerCount; ++i) {
    if (_printers[i].host[0] && strcasecmp(_printers[i].host, host) == 0) {
      return &_printers[i];
    }
  }

  return nullptr;
}

PrinterScannerScreen::Printer* PrinterScannerScreen::_addPrinter()
{
  if (_printerCount >= MAX_PRINTERS) return nullptr;

  Printer& p = _printers[_printerCount++];
  memset(&p, 0, sizeof(p));
  strncpy(p.name, "Network Printer", sizeof(p.name) - 1);
  return &p;
}

void PrinterScannerScreen::_mergeSsdp(const SsdpScanUtil::Device& dev)
{
  Printer* p = _findByIp(dev.ip);
  if (!p) p = _addPrinter();
  if (!p) return;

  if (dev.name[0] && strcmp(dev.name, "SSDP Device") != 0) {
    strncpy(p->name, dev.name, sizeof(p->name) - 1);
  }

  if (dev.ip[0]) {
    strncpy(p->ip, dev.ip, sizeof(p->ip) - 1);
  }

  strncpy(p->source, "SSDP", sizeof(p->source) - 1);
  strncpy(p->service, "UPnP Printer", sizeof(p->service) - 1);

  if (dev.server[0]) {
    strncpy(p->server, dev.server, sizeof(p->server) - 1);
  }

  if (dev.location[0]) {
    strncpy(p->location, dev.location, sizeof(p->location) - 1);
  }
}

void PrinterScannerScreen::_mergeMdns(const MdnsScanUtil::Service& svc)
{
  Printer* p = nullptr;

  if (svc.ip[0]) p = _findByIp(svc.ip);
  if (!p && svc.host[0]) p = _findByHost(svc.host);
  if (!p) p = _addPrinter();
  if (!p) return;

  if (svc.name[0]) {
    strncpy(p->name, svc.name, sizeof(p->name) - 1);
  }

  if (svc.ip[0]) {
    strncpy(p->ip, svc.ip, sizeof(p->ip) - 1);
  }

  if (svc.host[0]) {
    strncpy(p->host, svc.host, sizeof(p->host) - 1);
  }

  if (strcmp(p->source, "SSDP") == 0) {
    strncpy(p->source, "SSDP+mDNS", sizeof(p->source) - 1);
  } else {
    strncpy(p->source, "mDNS", sizeof(p->source) - 1);
  }

  if (svc.port) p->port = svc.port;

  if (svc.service[0]) {
    strncpy(p->service, svc.service, sizeof(p->service) - 1);
  }
}
