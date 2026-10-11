#pragma once

#include "ui/templates/ListScreen.h"

// Tools that reach out to the internet, as opposed to Services, which serve
// the local network. Everything here needs working upstream connectivity.
class NetworkInternetScreen : public ListScreen
{
public:
  const char* title() override { return "Internet"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[4] = {
    {"World Clock"},  // NTP + timezone browser
    {"Wikipedia"},    // article reader
    {"WiGLE"},        // wardrive upload, stats, map
    {"Download"},     // fetch assets: portals, IRDB, scripts
  };
};
