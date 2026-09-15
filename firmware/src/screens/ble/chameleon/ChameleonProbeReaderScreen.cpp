#include "ChameleonProbeReaderScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"

void ChameleonProbeReaderScreen::_drawWaiting() {
  auto& lcd = Uni.Lcd;
  const int bx=bodyX(), by=bodyY(), bw=bodyW(), bh=bodyH();
  lcd.fillRect(bx,by,bw,bh,TFT_BLACK);
  lcd.setTextDatum(MC_DATUM); lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
  lcd.drawString("Waiting for reader...", bx+bw/2, by+bh/2);
}
void ChameleonProbeReaderScreen::_setError(const char* msg) {
  _state=ERROR; _rowCount=0;
  _labels[0]="Status"; _values[0]=msg; _rows[0]={_labels[0].c_str(),_values[0]}; _rowCount=1;
  _scroll.setRows(_rows,_rowCount);
}
void ChameleonProbeReaderScreen::_restore() {
  if (!_armed) return;
  auto& c=ChameleonClient::get();
  if (_restoreDetection) c.mf1SetDetectEnable(_previousDetection);
  if (_restoreHfEnable) c.setSlotEnable(_probeSlot, 2, _previousHfEnable);
  if (_restoreSlot) c.setActiveSlot(_previousSlot);
  if (_restoreMode) c.setMode(_previousMode);
  _armed=false;
}
void ChameleonProbeReaderScreen::onInit() {
  _state=WAITING; _rowCount=0; _armed=false;
  auto& c=ChameleonClient::get();
  _restoreMode=c.getMode(&_previousMode);
  _restoreSlot=c.getActiveSlot(&_previousSlot);
  _restoreDetection=c.mf1GetDetectEnable(&_previousDetection);
  // Probe Reader temporarily mutates all three states.  If any original state
  // cannot be read, fail before changing the Chameleon so Back can never leave
  // the device in a different configuration.
  if (!_restoreMode || !_restoreSlot || !_restoreDetection) {
    _setError("Unable to save device state"); return;
  }
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) { _setError("Unable to read slots"); return; }
  bool found=false;
  for (uint8_t i=0;i<8;i++) {
    if (types[i].hfType==1000 || types[i].hfType==1001 || types[i].hfType==1002 || types[i].hfType==1003) {
      _probeSlot=i; found=true; break;
    }
  }
  if (!found) { _setError("No MIFARE Classic slot"); return; }
  bool hfEn[8] = {}, lfEn[8] = {};
  if (!c.getEnabledSlots(hfEn, lfEn)) {
    _setError("Unable to read slot state"); return;
  }
  _previousHfEnable = hfEn[_probeSlot];
  _restoreHfEnable = true;
  _armed=true; // from here on, every exit path must restore slot/mode/detection state
  if (!c.setActiveSlot(_probeSlot) ||
      (_restoreHfEnable && !_previousHfEnable && !c.setSlotEnable(_probeSlot, 2, true)) ||
      !c.mf1GetDetectCount(&_baseline) || !c.mf1SetDetectEnable(true) || !c.setMode(0)) {
    _setError("Probe unavailable"); _restore(); return;
  }
  _lastPoll=0;
}
void ChameleonProbeReaderScreen::_showRecord(uint32_t index) {
  uint8_t rec[18] = {};
  if (!ChameleonClient::get().mf1GetDetectRecord(index,rec)) {
    _setError("Unable to read detection");
    return;
  }
  _state=RESULT; _rowCount=0;
  _labels[_rowCount]="Technology"; _values[_rowCount]="ISO 14443-A"; _rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]}; _rowCount++;
  _labels[_rowCount]="Likely tag"; _values[_rowCount]="MIFARE Classic"; _rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]}; _rowCount++;
  _labels[_rowCount]="Confidence"; _values[_rowCount]="High"; _rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]}; _rowCount++;
  // Detection records are proof that a Classic authentication exchange reached the emulator.
  _labels[_rowCount]="Reader action"; _values[_rowCount]="Authentication"; _rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]}; _rowCount++;
  _scroll.setRows(_rows,_rowCount);
}
void ChameleonProbeReaderScreen::onUpdate() {
  if (Uni.Nav->wasPressed()) {
    auto d=Uni.Nav->readDirection();
    if (d==INavigation::DIR_BACK) { _restore(); Screen.goBack(); return; }
    if (_state==RESULT || _state==ERROR) _scroll.onNav(d);
  }
  if (_state!=WAITING || !_armed || millis()-_lastPoll<500) return;
  _lastPoll=millis(); uint32_t count=0;
  if (ChameleonClient::get().mf1GetDetectCount(&count) && count>_baseline) {
    _showRecord(count-1); _restore();
  }
}
void ChameleonProbeReaderScreen::onRender() {
  if (_state==WAITING) _drawWaiting(); else _scroll.render(bodyX(),bodyY(),bodyW(),bodyH());
}
ChameleonProbeReaderScreen::~ChameleonProbeReaderScreen() { _restore(); }
