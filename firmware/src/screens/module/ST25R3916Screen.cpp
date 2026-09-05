#include "ST25R3916Screen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

void ST25R3916Screen::onInit() {
  setItems(_items);
}

void ST25R3916Screen::onItemSelected(uint8_t index) {
  if (index == 0) _showDeviceInfo();
}

void ST25R3916Screen::onBack() {
  _backend.end();
  Screen.goBack();
}

void ST25R3916Screen::_showDeviceInfo() {
#ifdef DEVICE_HAS_ST25R3916
  ShowStatusAction::show("Initializing ST25R3916...", 0);

  if (!Uni.Spi) {
    ShowStatusAction::show("SPI bus unavailable", 1400);
    render();
    return;
  }

  if (_backend.begin(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, 10000000)) {
    ShowStatusAction::show("ST25R3916 detected", 1400);
  } else {
    char msg[48];
    snprintf(msg, sizeof(msg), "ST25R3916 not found (%d)", _backend.lastError());
    ShowStatusAction::show(msg, 1800);
  }
#else
  ShowStatusAction::show("ST25R3916 unsupported", 1400);
#endif
  render();
}
