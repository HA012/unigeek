#include "ChameleonMfuMenuScreen.h"
#include "ChameleonMfuToolsScreen.h"
#include "ChameleonMfuNdefScreen.h"
#include "core/ScreenManager.h"
void ChameleonMfuMenuScreen::onInit(){setItems(_items);}
void ChameleonMfuMenuScreen::onItemSelected(uint8_t i){if(i==0)Screen.push(new ChameleonMfuToolsScreen());else if(i==1)Screen.push(new ChameleonMfuNdefScreen());}
void ChameleonMfuMenuScreen::onBack(){Screen.goBack();}
