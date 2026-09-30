#include "ChameleonScanReaderScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/ble/ChameleonClient.h"

void ChameleonScanReaderScreen::_drawWaiting() {
  ShowStatusAction::show("Waiting for reader...", 0);
}
void ChameleonScanReaderScreen::_setError(const char* msg) {
  _state=ERROR; _rowCount=0;
  _labels[0]="Status"; _values[0]=msg; _rows[0]={_labels[0].c_str(),_values[0]}; _rowCount=1;
  _scroll.setRows(_rows,_rowCount);
}
void ChameleonScanReaderScreen::_restore() {
  if (!_armed) return;
  auto& c=ChameleonClient::get();
  if (_restoreDetection) c.mf1SetDetectEnable(_previousDetection);
  if (_restoreHfType) {
    c.setSlotTagType(_probeSlot, _previousHfType);
    if (_previousHfType != 0) c.setSlotDataDefault(_probeSlot, _previousHfType);
  }
  if (_restoreHfEnable) c.setSlotEnable(_probeSlot, 2, _previousHfEnable);
  if (_restoreSlot) c.setActiveSlot(_previousSlot);
  if (_restoreMode) c.setMode(_previousMode);
  _armed=false;
}
void ChameleonScanReaderScreen::onInit() {
  ShowStatusAction::show("Loading...", 0);
  _state=WAITING; _rowCount=0; _armed=false;
  auto& c=ChameleonClient::get();
  _restoreMode=c.getMode(&_previousMode);
  _restoreSlot=c.getActiveSlot(&_previousSlot);
  _restoreDetection=c.mf1GetDetectEnable(&_previousDetection);
  // Detection enable query is optional on older firmware; assume off.
  if (!_restoreDetection) _previousDetection = false;
  // MF1 Detector temporarily mutates slot/mode/detection state. Preserve
  // available state and restore it when leaving the screen.
  if (!_restoreMode || !_restoreSlot) {
    _setError("Failed to read device state"); return;
  }
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) { _setError("Unable to read slots"); return; }
  auto isClassic = [](uint16_t t) {
    return t==1000 || t==1001 || t==1002 || t==1003;
  };
  bool found=false;
  for (uint8_t i=0;i<8;i++) {
    if (isClassic(types[i].hfType)) {
      _probeSlot=i; found=true; break;
    }
  }
  if (!found) {
    // Provision a temporary MF-1K emulator so the detector works even when
    // no Classic slot is already configured.
    int8_t empty = -1;
    for (uint8_t i=0;i<8;i++) {
      if (types[i].hfType==0) { empty = (int8_t)i; break; }
    }
    _probeSlot = (empty >= 0) ? (uint8_t)empty : (uint8_t)7;
    _previousHfType = types[_probeSlot].hfType;
    _restoreHfType = true;
    if (!c.setSlotTagType(_probeSlot, 1001) ||
        !c.setSlotDataDefault(_probeSlot, 1001)) {
      _setError("Failed to prepare MIFARE slot"); return;
    }
    _initedDefault = true;
  }
  bool hfEn[8] = {}, lfEn[8] = {};
  if (!c.getEnabledSlots(hfEn, lfEn)) {
    _setError("Unable to read slot state"); return;
  }
  _previousHfEnable = hfEn[_probeSlot];
  _restoreHfEnable = true;
  _armed=true; // from here on, every exit path must restore slot/mode/detection state
  if (!c.setActiveSlot(_probeSlot)) {
    _setError("Failed to select MIFARE slot"); _restore(); return;
  }
  if (!_previousHfEnable && !c.setSlotEnable(_probeSlot, 2, true)) {
    _setError("Failed to enable slot"); _restore(); return;
  }
  if (!c.mf1SetDetectEnable(true)) {
    _setError("Failed to enable MF1 detector"); _restore(); return;
  }
  // Official firmware clears the detection log when enabling it, so the
  // baseline is always 0. Skip GET_DETECTION_COUNT here: CHANGE_MODE can
  // stall the next BLE command and produced "Failed to read detector state".
  _baseline = 0;
  if (!c.setMode(0)) {
    _setError("Failed to enter emulator mode"); _restore(); return;
  }
  _lastPoll=0;
  _probeStartedAt=millis();
}
void ChameleonScanReaderScreen::_showRecord(uint32_t index) {
  uint8_t rec[18] = {};
  if (!ChameleonClient::get().mf1GetDetectRecord(index,rec)) {
    _setError("MIFARE Classic reader not detected");
    return;
  }
  _state=RESULT; _rowCount=0;
  _labels[_rowCount]="Technology"; _values[_rowCount]="MIFARE Classic"; _rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]}; _rowCount++;
  _labels[_rowCount]="Confidence"; _values[_rowCount]="High"; _rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]}; _rowCount++;
  // Detection records are proof that a Classic authentication exchange reached the emulator.
  _labels[_rowCount]="Action"; _values[_rowCount]="Authentication"; _rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]}; _rowCount++;
  _scroll.setRows(_rows,_rowCount);
}
void ChameleonScanReaderScreen::onUpdate() {
  if (Uni.Nav->wasPressed()) {
    auto d=Uni.Nav->readDirection();
    if (d==INavigation::DIR_BACK ||
        ((d==INavigation::DIR_PRESS) && (_state==RESULT || _state==ERROR))) {
      _restore();
      Screen.goBack();
      return;
    }
    if (_state==RESULT || _state==ERROR) _scroll.onNav(d);
  }
  if (_state!=WAITING || !_armed) return;
  if (millis()-_probeStartedAt >= 15000) {
    _restore();
    ShowStatusAction::show("MIFARE Classic reader not detected", 1200);
    Screen.goBack();
    return;
  }
  if (millis()-_lastPoll<500) return;
  _lastPoll=millis(); uint32_t count=0;
  if (ChameleonClient::get().mf1GetDetectCount(&count) && count>_baseline) {
    _showRecord(count-1); _restore();
  }
}
void ChameleonScanReaderScreen::onRender() {
  if (_state==WAITING) _drawWaiting(); else _scroll.render(bodyX(),bodyY(),bodyW(),bodyH());
}
ChameleonScanReaderScreen::~ChameleonScanReaderScreen() { _restore(); }
