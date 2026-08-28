#pragma once

#include "ui/templates/ListScreen.h"

// Discovery tools: find what is on the network before touching it.
// Grouped so the Network menu isn't 17 flat entries.
class NetworkScannersScreen : public ListScreen
{
public:
  const char* title() override { return "Scanners"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[10] = {
    {"IP Scan"},
    {"Port Scan"},
    {"mDNS"},
    {"SSDP"},
    {"Web Servers"},
    {"Remote Shells"},
    {"File Servers"},
    {"IoT Devices"},
    {"Printers"},
    {"CCTV"},
  };
};
