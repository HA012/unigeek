#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/nfc/MagicCard.h"
#include <Arduino.h>

class ChameleonMfcUidWriteScreen : public BaseScreen {
public:
  explicit ChameleonMfcUidWriteScreen(const String& path) : _path(path), _fromFile(true) {}
  ChameleonMfcUidWriteScreen(const uint8_t* uid, uint8_t uidLen);
  const char* title() override { return "Write UID to Tag"; }
  bool inhibitPowerOff() override { return _busy; }
  void onInit() override; void onUpdate() override; void onRender() override;
private:
  String _path; bool _fromFile=false, _busy=false; uint8_t _uid[7]={}, _uidLen=0;
  ScrollListView _view; ScrollListView::Row _rows[4]; String _l[4],_v[4]; uint8_t _count=0;
  bool load(); void preview(); void write(); bool readGen1aBlock0(uint8_t out[16]); String uidText() const;
};
