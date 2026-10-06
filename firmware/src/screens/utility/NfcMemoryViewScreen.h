#pragma once

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class NfcMemoryViewScreen : public BaseScreen {
public:
  enum Layout { MIFARE_CLASSIC, TYPE2 };

  NfcMemoryViewScreen(Layout layout, const String& type, const uint8_t* uid, uint8_t uidLen,
                      const uint8_t* data, size_t dataLen, const uint8_t* validUnits = nullptr);
  ~NfcMemoryViewScreen() override;

  const char* title() override { return "Read Memory"; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr uint16_t MAX_ROWS = 520;
  Layout _layout;
  String _type;
  uint8_t _uid[10] = {};
  uint8_t _uidLen = 0;
  uint8_t* _data = nullptr;
  size_t _dataLen = 0;
  uint8_t* _validUnits = nullptr;
  bool _validityProvided = false;
  size_t _unitCount = 0;
  ScrollListView _view;
  ScrollListView::Row _rows[MAX_ROWS];
  String _labels[MAX_ROWS];
  String _values[MAX_ROWS];
  uint16_t _rowCount = 0;

  void addRow(const String& label, const String& value);
  void buildRows();
};
