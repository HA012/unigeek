#include "ChameleonLFMenuScreen.h"
#include "ChameleonLFScanScreen.h"
#include "ChameleonT5577WriteScreen.h"
#include "ChameleonT5577CleanerScreen.h"
#include "core/ScreenManager.h"

void ChameleonLFMenuScreen::onInit() {
  _items[0] = {"Read Tag"};
  _items[1] = {"Write to Tag (T5577)"};
  _items[2] = {"Password Recovery (T5577)"};
  setItems(_items);
}
void ChameleonLFMenuScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonLFScanScreen());
  else if (index == 1) Screen.push(new ChameleonT5577WriteScreen());
  else if (index == 2) Screen.push(new ChameleonT5577CleanerScreen());
}
void ChameleonLFMenuScreen::onBack() { Screen.goBack(); }
