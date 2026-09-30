#include "ChameleonHFMenuScreen.h"
#include "ChameleonHFScreen.h"
#include "ChameleonMenuScreen.h"
#include "ChameleonMfcMenuScreen.h"
#include "ChameleonMfuMenuScreen.h"
#include "core/ScreenManager.h"

void ChameleonHFMenuScreen::onInit() {
  _items[0] = {"Scan Tag"};
  _items[1] = {"MIFARE Classic"};
  _items[2] = {"Ultralight / NTAG"};
  setItems(_items);
}

void ChameleonHFMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonHFScreen());      break;
    case 1: Screen.push(new ChameleonMfcMenuScreen()); break;
    case 2: Screen.push(new ChameleonMfuMenuScreen()); break;
  }
}

void ChameleonHFMenuScreen::onBack() {
  Screen.goBack();
}
