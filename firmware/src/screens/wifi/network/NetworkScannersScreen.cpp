#include "NetworkScannersScreen.h"
#include "core/ScreenManager.h"
#include "IPScannerScreen.h"
#include "PortScannerScreen.h"
#include "SsdpScannerScreen.h"
#include "MdnsScannerScreen.h"
#include "PrinterScannerScreen.h"
#include "CctvSnifferScreen.h"
#include "WebServerScannerScreen.h"
#include "RemoteShellScannerScreen.h"
#include "ui/actions/ShowStatusAction.h"

void NetworkScannersScreen::onInit() {
  setItems(_items);
}

void NetworkScannersScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new IPScannerScreen()); break;
    case 1: Screen.push(new PortScannerScreen()); break;
    case 2: Screen.push(new MdnsScannerScreen()); break;
    case 3: Screen.push(new SsdpScannerScreen()); break;
    case 4: Screen.push(new WebServerScannerScreen()); break;
    case 5: Screen.push(new RemoteShellScannerScreen()); break;
    case 6: ShowStatusAction::show("Not implemented yet"); break;
    case 7: Screen.push(new PrinterScannerScreen()); break;
    case 8: Screen.push(new CctvSnifferScreen()); break;
  }
}

void NetworkScannersScreen::onBack() {
  Screen.goBack();
}
