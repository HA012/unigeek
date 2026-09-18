#include "ChameleonLFProtocolMenuScreen.h"
#include "ChameleonLFScreen.h"
#include "ChameleonHIDProxScreen.h"
#include "ChameleonVikingScreen.h"
#include "ChameleonLFExtendedScreen.h"
#include "core/ScreenManager.h"
const char* ChameleonLFProtocolMenuScreen::title() { switch (_protocol) { case EM410X:return "EM410X"; case HID_PROX:return "HID Prox"; case IOPROX:return "ioProx"; case VIKING:return "Viking"; case PAC_STANLEY:return "PAC/Stanley"; default:return "Jablotron"; } }
void ChameleonLFProtocolMenuScreen::onInit() {
  _items[0] = {"Read Tag"}; _items[1] = {"Load to Slot"}; _items[2] = {"Write to Tag (T5577)"}; setItems(_items);
}
void ChameleonLFProtocolMenuScreen::onItemSelected(uint8_t index) {
  if (index > 2) return;
  if (_protocol == EM410X) Screen.push(new ChameleonLFScreen((ChameleonLFScreen::Operation)index));
  else if (_protocol == HID_PROX) Screen.push(new ChameleonHIDProxScreen((ChameleonHIDProxScreen::Operation)index));
  else if (_protocol == VIKING) Screen.push(new ChameleonVikingScreen((ChameleonVikingScreen::Operation)index));
  else { ChameleonLFExtendedScreen::Protocol p = _protocol == IOPROX ? ChameleonLFExtendedScreen::IOPROX : (_protocol == PAC_STANLEY ? ChameleonLFExtendedScreen::PAC_STANLEY : ChameleonLFExtendedScreen::JABLOTRON); Screen.push(new ChameleonLFExtendedScreen(p, (ChameleonLFExtendedScreen::Operation)index)); }
}
void ChameleonLFProtocolMenuScreen::onBack() { Screen.goBack(); }
