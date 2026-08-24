#pragma once

#include "ui/templates/ListScreen.h"

class SshClientScreen : public ListScreen
{
public:
  const char* title() override { return "SSH Client"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  String _host;
  String _username;
  int    _port = 22;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};
  char _userLabel[32] = {};
  char _authLabel[16] = "Password";
  ListItem _items[5];

  void _updateLabels();
  void _rebuildItems();
  void _configHost();
  void _configPort();
  void _configUsername();
  void _auth();    // TODO: later Password / Private Key selector
  void _connect(); // TODO: libssh session + PTY/shell
};
