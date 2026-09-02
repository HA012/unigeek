#include "BLEMonitoringDetectionMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLEAnalyzerScreen.h"
#include "screens/ble/BLEDetectorScreen.h"

void BLEMonitoringDetectionMenuScreen::onInit()
{
  setItems(_items);
}

void BLEMonitoringDetectionMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new BLEAnalyzerScreen()); break;
    case 1: Screen.push(new BLEDetectorScreen()); break;
  }
}

void BLEMonitoringDetectionMenuScreen::onBack()
{
  Screen.goBack();
}
