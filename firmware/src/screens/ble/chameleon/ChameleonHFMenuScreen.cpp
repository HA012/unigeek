#include "ChameleonHFMenuScreen.h"
#include "ChameleonHFScreen.h"
#include "ChameleonMenuScreen.h"
#include "ChameleonMfcMenuScreen.h"
#include "ChameleonMfuMenuScreen.h"
#include "core/ScreenManager.h"

void ChameleonHFMenuScreen::onInit() {
  _mainItems[0] = {"Scan Tag"};
  _mainItems[1] = {"Read Tag"};
  _mainItems[2] = {"Families"};
  _items[0] = {"MIFARE Classic"};
  _items[1] = {"Ultralight / NTAG"};
  setItems(_mainItems, 3, _mainSelection);
}

void ChameleonHFMenuScreen::onItemSelected(uint8_t index) {
  if (!_families) {
    _mainSelection = index;
    if (index == 0) Screen.push(new ChameleonHFScreen());
    else if (index == 1) Screen.push(new ChameleonHFScreen(true));
    else {
      _families = true;
      setItems(_items, 2, _familySelection);
    }
    return;
  }
  _familySelection = index;
  if (index == 0) Screen.push(new ChameleonMfcMenuScreen());
  else if (index == 1) Screen.push(new ChameleonMfuMenuScreen());
}

void ChameleonHFMenuScreen::onBack() {
  if (_families) {
    _families = false;
    setItems(_mainItems, 3, _mainSelection);
  } else Screen.goBack();
}
