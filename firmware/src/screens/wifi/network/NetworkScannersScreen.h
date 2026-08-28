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
  ListItem _items[6] = {
    {"IP Hosts"},     // live hosts on the subnet
    {"Port Scan"},   // open ports on one host
    {"SSDP"},   // UPnP/SSDP devices and services
    {"mDNS"},   // Bonjour / DNS-SD service discovery
    {"Printers"},// printers via SSDP + IPP/mDNS
    {"CCTV Sniffer"},   // network cameras: brand, creds, live view
  };
};
