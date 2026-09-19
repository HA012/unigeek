#include "ChameleonT5577CleanerScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/StatusBar.h"

static constexpr const char* kDictDir = "/unigeek/rfid/dictionaries";
static constexpr uint8_t kBuiltinPasswords[][4] = {
  {0x51,0x24,0x36,0x48}, {0x00,0x00,0x00,0x00}, {0xAA,0xAA,0xAA,0xAA},
  {0x55,0x55,0x55,0x55}, {0x12,0x34,0x56,0x78}, {0xFF,0xFF,0xFF,0xFF},
  {0x19,0x92,0x04,0x27}, {0x01,0x23,0x45,0x67}, {0xAB,0xCD,0xEF,0x01},
  {0xC6,0xB6,0xF9,0x2E},
};

static bool parseT55xxKey(const String& line, uint8_t out[4]) {
  String s = line; s.trim();
  if (!s.length() || s.startsWith("#")) return false;
  s.replace(":", ""); s.replace(" ", "");
  if (s.length() != 8) return false;
  for (int i=0;i<4;i++) {
    char hex[3] = {s[i*2], s[i*2+1], 0}; char* end=nullptr;
    unsigned long v=strtoul(hex,&end,16); if (*end) return false; out[i]=(uint8_t)v;
  }
  return true;
}

void ChameleonT5577CleanerScreen::_loadPicker() {
  if (!_pickDir.length()) _pickDir = kDictDir;
  _browser.root = kDictDir;
  uint8_t n = _browser.load(this, _pickDir, ".txt", nullptr, BrowseFileView::STEM_CAPITALIZED);
  uint8_t off = 0;
  if (_pickDir == kDictDir) { _items[0] = {"Built-in Keys"}; off = 1; }
  for (uint8_t i=0;i<n;i++) _items[i+off] = _browser.items()[i];
  setItems(_items, n+off);
}

void ChameleonT5577CleanerScreen::onInit() { _state=STATE_SELECT; _loadPicker(); }

void ChameleonT5577CleanerScreen::onBack() {
  if (_state == STATE_SELECT) {
    if (_pickDir == kDictDir || !_pickDir.length()) { _pickDir=""; Screen.goBack(); return; }
    int slash=_pickDir.lastIndexOf('/'); _pickDir=(slash>0)?_pickDir.substring(0,slash):kDictDir;
    _loadPicker(); render(); return;
  }
  _state=STATE_SELECT; _loadPicker(); render();
}

void ChameleonT5577CleanerScreen::onUpdate() {
  if (_state == STATE_RUNNING) return;
  if (_state == STATE_DONE) {
    if (!Uni.Nav->wasPressed()) return;
    auto dir=Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _state=STATE_SELECT; _loadPicker(); render(); return;
    }
    if (dir == INavigation::DIR_UP) _log.scroll(1);
    if (dir == INavigation::DIR_DOWN) _log.scroll(-1);
    _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH()); StatusBar::refresh();
    return;
  }
  ListScreen::onUpdate();
}

void ChameleonT5577CleanerScreen::onRender() {
  if (_state == STATE_RUNNING || _state == STATE_DONE) {
    _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH()); StatusBar::refresh(); return;
  }
  ListScreen::onRender();
}

bool ChameleonT5577CleanerScreen::_loadBuiltIn() {
  _keyCount = sizeof(kBuiltinPasswords)/4;
  memcpy(_keys, kBuiltinPasswords, sizeof(kBuiltinPasswords)); return true;
}

bool ChameleonT5577CleanerScreen::_loadFile(const char* path) {
  _keyCount=0; if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String content=Uni.Storage->readFile(path); if (!content.length()) return false;
  int start=0;
  while (start < (int)content.length() && _keyCount < MAX_KEYS) {
    int nl=content.indexOf('\n',start); if (nl<0) nl=content.length();
    uint8_t key[4]; if (parseT55xxKey(content.substring(start,nl),key)) memcpy(_keys[_keyCount++],key,4);
    start=nl+1;
  }
  return _keyCount>0;
}

void ChameleonT5577CleanerScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_SELECT) return;
  uint8_t off=(_pickDir==kDictDir)?1:0; String label;
  if (off && index==0) { _loadBuiltIn(); label="Built-in"; }
  else {
    uint8_t fi=index-off; if (fi>=_browser.count()) return;
    const auto& e=_browser.entry(fi);
    if (e.isDir) { _pickDir=e.path; _loadPicker(); render(); return; }
    if (!_loadFile(e.path.c_str())) { ShowStatusAction::show("Failed to load keys",1200); render(); return; }
    label=e.name;
  }
  if (!_keyCount) { ShowStatusAction::show("No keys in source",1200); render(); return; }
  _run(label.c_str());
}

void ChameleonT5577CleanerScreen::_run(const char* sourceLabel) {
  _state=STATE_RUNNING; _running=true; _log.clear();
  String src=String("Src: ")+sourceLabel; _log.addLine(src.c_str(),TFT_CYAN);
  _log.addLine("Place T5577 tag on reader...",TFT_DARKGREY); render();
  static const uint8_t dummyUid[5]={0xAA,0xAA,0xAA,0xAA,0xAA};
  static const uint8_t newPw[4]={0x51,0x24,0x36,0x48};
  auto& c=ChameleonClient::get(); c.setMode(1);
  bool success=false; char msg[48];
  for (uint16_t i=0;i<_keyCount;i++) {
    snprintf(msg,sizeof(msg),"Try pw %02X%02X%02X%02X",_keys[i][0],_keys[i][1],_keys[i][2],_keys[i][3]);
    _log.addLine(msg,TFT_WHITE); onRender();
    if (c.writeEM410XToT5577(dummyUid,newPw,_keys[i],1)) {
      snprintf(msg,sizeof(msg),"Recovered w/ key #%u",(unsigned)i); _log.addLine(msg,TFT_GREEN); success=true; break;
    }
  }
  if (!success) _log.addLine("All passwords failed",TFT_RED);
  else { int n=Achievement.inc("chameleon_t5577_clean"); if (n==1) Achievement.unlock("chameleon_t5577_clean"); }
  _running=false; _state=STATE_DONE; render();
}
