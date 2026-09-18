#include "ChameleonLFMenuScreen.h"
#include "ChameleonLFProtocolMenuScreen.h"
#include "ChameleonT5577MenuScreen.h"
#include "core/ScreenManager.h"
void ChameleonLFMenuScreen::onInit() {
  _items[0] = {"EM410X"}; _items[1] = {"HID Prox"}; _items[2] = {"Viking"}; _items[3] = {"T5577"}; setItems(_items);
}
void ChameleonLFMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::EM410X)); break;
    case 1: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::HID_PROX)); break;
    case 2: Screen.push(new ChameleonLFProtocolMenuScreen(ChameleonLFProtocolMenuScreen::VIKING)); break;
    case 3: Screen.push(new ChameleonT5577MenuScreen()); break;
  }
}
void ChameleonLFMenuScreen::onBack() { Screen.goBack(); }
