#include "ChameleonT5577MenuScreen.h"
#include "ChameleonT5577WriteScreen.h"
#include "ChameleonT5577CleanerScreen.h"
#include "core/ScreenManager.h"
void ChameleonT5577MenuScreen::onInit() { _items[0] = {"Write to Tag"}; _items[1] = {"Password Cleaner"}; setItems(_items); }
void ChameleonT5577MenuScreen::onItemSelected(uint8_t index) { if (index == 0) Screen.push(new ChameleonT5577WriteScreen()); else if (index == 1) Screen.push(new ChameleonT5577CleanerScreen()); }
void ChameleonT5577MenuScreen::onBack() { Screen.goBack(); }
