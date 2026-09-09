#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"

class ST25R3916Screen : public ListScreen {
public:
  const char* title() override {
    return _state == STATE_SCANNING ? "Scan Tag" : (_state == STATE_DETAILS ? "Tag Details" : "ST25R3916");
  }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t { STATE_MENU, STATE_SCANNING, STATE_DETAILS };

  State _state = STATE_MENU;
  uint16_t _lastTechMask = 0;

  ListItem _items[7] = {
    {"Scan Tag", "Auto I2C / SPI"},
    {"NFC-A", "ISO14443A"},
    {"NFC-B", "ISO14443B"},
    {"NFC-F / FeliCa", "212 kbps"},
    {"NFC-V / ISO15693"},
    {"Device Info (I2C)", "Grove / U216"},
    {"Device Info (SPI)", "Cap / shared SPI"},
  };

  static constexpr uint8_t kMaxRows = 9;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _rowLabels[kMaxRows];
  String _rowValues[kMaxRows];
  uint8_t _rowCount = 0;

  void _scan(uint16_t techMask);
  void _showI2CInfo();
  void _showSPIInfo();
  void _showMenu();
  void _renderTagPrompt();
};
