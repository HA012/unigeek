#pragma once

#include "ui/templates/ListScreen.h"
#include "core/IScreen.h"

class PinSettingsScreen : public ListScreen
{
public:
  using BackFactory = IScreen*(*)();

  PinSettingsScreen() = default;
  explicit PinSettingsScreen(BackFactory backFn) : _backFn(backFn) {}

  const char* title() override { return "Pin Settings"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  void _refresh();

  BackFactory _backFn = nullptr;

  static const uint8_t MAX_ITEMS = 16;
  ListItem _items[MAX_ITEMS];
  uint8_t _itemCount = 0;

  // track which config each index maps to
  enum PinType { PIN_GPS_TX, PIN_GPS_RX, PIN_GPS_BAUD, PIN_EXT_SDA, PIN_EXT_SCL,
                 PIN_CC1101_CS, PIN_CC1101_GDO0,
                 PIN_NRF24_CE, PIN_NRF24_CSN,
                 PIN_PN532_TX, PIN_PN532_RX, PIN_PN532_BAUD,
                 PIN_IR_TX, PIN_IR_RX,
                 PIN_GROVE_5V };
  PinType _map[MAX_ITEMS];

  String _gpsTxSub;
  String _gpsRxSub;
  String _gpsBaudSub;
  String _sdaSub;
  String _sclSub;
  String _cc1101CsSub;
  String _cc1101Gdo0Sub;
  String _nrf24CeSub;
  String _nrf24CsnSub;
  String _pn532TxSub;
  String _pn532RxSub;
  String _pn532BaudSub;
  String _irTxSub;
  String _irRxSub;
  String _grove5VSub;
};