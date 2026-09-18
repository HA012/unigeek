#include "ChameleonLFMenuScreen.h"
#include "ChameleonLFScanScreen.h"
#include "ChameleonLFProtocolMenuScreen.h"
#include "ChameleonT5577MenuScreen.h"
#include "core/ScreenManager.h"
void ChameleonLFMenuScreen::onInit() {
  _items[0] = {"Scan Tag"}; _items[1] = {"EM410X"}; _items[2] = {"HID Prox"}; _items[3] = {"ioProx"}; _items[4] = {"Viking"}; _items[5] = {"PAC/Stanley"}; _items[6] = {"Jablotron"}; _items[7] = {"T5577"}; setItems(_items);
}
void ChameleonLFMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonLFScanScreen()); break;
    case 1: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::EM410X)); break;
    case 2: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::HID_PROX)); break;
    case 3: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::IOPROX)); break;
    case 4: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::VIKING)); break;
    case 5: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::PAC_STANLEY)); break;
    case 6: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::JABLOTRON)); break;
    case 7: Screen.push(new ChameleonT5577MenuScreen()); break;
  }
}
void ChameleonLFMenuScreen::onBack() { Screen.goBack(); }
