#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/ble/ChameleonClient.h"

class ChameleonMfcHardNestedScreen : public BaseScreen {
public:
  ChameleonMfcHardNestedScreen() = default;
  ChameleonMfcHardNestedScreen(const uint8_t* uid, uint8_t uidLen, uint8_t sectors,
                               const bool* foundA = nullptr,
                               const bool* foundB = nullptr);
  const char* title() override { return "Hard Nested"; }
  bool inhibitPowerOff() override { return _busy; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
private:
  uint8_t _uid[7] = {};
  uint8_t _uidLen = 0;
  uint8_t _sectors = 16;
  bool _busy = false;
  String _status;
  ScrollListView _scroll;
  ScrollListView::Row _rows[6];
  String _labels[6], _values[6];
  uint8_t _rowCount = 0;
  void _build();
  void _run();
};
