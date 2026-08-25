#include "RemoteAccessScreen.h"
#include "core/ScreenManager.h"
#include "screens/wifi/network/remote/TcpClientScreen.h"
#include "screens/wifi/network/remote/TelnetClientScreen.h"
#include "screens/wifi/network/remote/SshClientScreen.h"
#include "screens/wifi/network/remote/SftpClientScreen.h"

void RemoteAccessScreen::onInit() {
  setItems(_items);
}

void RemoteAccessScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new TcpClientScreen());    break;
    case 1: Screen.push(new TelnetClientScreen()); break;
    case 2: Screen.push(new SshClientScreen());    break;
    case 3: Screen.push(new SftpClientScreen());   break;
    default: break;
  }
}

void RemoteAccessScreen::onBack() {
  Screen.goBack();
}
