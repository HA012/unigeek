#include "ChameleonMfcAttacksScreen.h"
#include "ChameleonMfcScreen.h"
void ChameleonMfcAttacksScreen::onInit() {
  _items[0] = {"Dictionary Attack"}; _items[1] = {"Static Nested"}; _items[2] = {"Nested Attack"}; setItems(_items);
}
void ChameleonMfcAttacksScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_DICTIONARY));
  else if (index == 1) Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_STATIC_NESTED));
  else if (index == 2) Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_NESTED));
}
