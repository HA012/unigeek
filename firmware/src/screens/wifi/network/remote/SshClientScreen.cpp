#include "SshClientScreen.h"

#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <WiFi.h>

void SshClientScreen::onInit() {
  _updateLabels();
  _rebuildItems();
}

void SshClientScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _configHost();     break;
    case 1: _configPort();     break;
    case 2: _configUsername(); break;
    case 3: _auth();           break;
    case 4: _connect();        break;
    default: break;
  }
}

void SshClientScreen::onBack() {
  Screen.goBack();
}

void SshClientScreen::_updateLabels() {
  if (_host.length() == 0) snprintf(_hostLabel, sizeof(_hostLabel), "tap to set");
  else                     _host.toCharArray(_hostLabel, sizeof(_hostLabel));

  if (_username.length() == 0) snprintf(_userLabel, sizeof(_userLabel), "tap to set");
  else                         _username.toCharArray(_userLabel, sizeof(_userLabel));

  snprintf(_portLabel, sizeof(_portLabel), "%d", _port);
}

void SshClientScreen::_rebuildItems() {
  uint8_t sel = _selectedIndex;
  _items[0] = {"Host", _hostLabel};
  _items[1] = {"Port", _portLabel};
  _items[2] = {"Username", _userLabel};
  _items[3] = {"Auth", _authLabel};
  _items[4] = {"Connect", nullptr};
  setItems(_items, 5, sel);
}

void SshClientScreen::_configHost() {
  String initial = _host;
  if (initial.length() == 0 && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    initial = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
  }

  String value = InputTextAction::popup("Host", initial, InputTextAction::INPUT_IP_ADDRESS);
  if (!InputTextAction::wasCancelled() && value.length() > 0) {
    _host = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port);
  if (!InputNumberAction::wasCancelled()) {
    _port = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_configUsername() {
  String value = InputTextAction::popup("Username", _username, InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled() && value.length() > 0) {
    _username = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_auth() {
  // TODO: Password / Private Key selection once the libssh integration is added.
  ShowStatusAction::show("SSH Auth: TODO", 1200);
  render();
}

void SshClientScreen::_connect() {
  // TODO: libssh session, host-key verification, authentication, PTY and shell.
  ShowStatusAction::show("SSH Client: TODO", 1200);
  render();
}
