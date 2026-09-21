#include "ChameleonMfcToolsScreen.h"
#include "ChameleonMfcScreen.h"
#include "ChameleonMfcWriteScreen.h"
#include "ChameleonMfcUidWriteScreen.h"
#include "ChameleonMagicScreen.h"
#include "ChameleonMfcAdvancedScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "utils/nfc/NfcDumpParser.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/views/ProgressView.h"


namespace {
static constexpr uint8_t kMfc1kSectors = 16;

static constexpr uint8_t kEraseKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97},
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
  {0xA0,0x47,0x8C,0xC3,0x90,0x91},
  {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
  {0x8F,0xD0,0xA4,0xF2,0x56,0xE9},
  {0x00,0x00,0x00,0x00,0x00,0x01},
  {0x11,0x22,0x33,0x44,0x55,0x66},
  {0x26,0x97,0x34,0x3B,0x00,0x00},
  {0x12,0x34,0x56,0x78,0x9A,0xBC},
  {0xBD,0x49,0x3A,0x39,0x62,0xB6},
};
static constexpr uint8_t kEraseKeyCount =
    sizeof(kEraseKeys) / sizeof(kEraseKeys[0]);

static uint8_t _mfcTrailer(uint8_t sector) {
  return sector * 4u + 3u;
}

static bool _mfcWriteWithKnownKey(
    ChameleonClient& c, uint8_t block, const uint8_t data[16],
    const uint8_t keyA[6], bool hasA,
    const uint8_t keyB[6], bool hasB) {
  if (hasA && c.mf1WriteBlock(block, 0x60, keyA, data)) return true;
  if (hasB && c.mf1WriteBlock(block, 0x61, keyB, data)) return true;
  return false;
}
}

void ChameleonMfcToolsScreen::onInit() {
  _items[0] = {"Detect Magic"};
  _items[1] = {"Read Tag"};
  _items[2] = {"Write UID to Tag"};
  _items[3] = {"Write Dump to Tag"};
  _items[4] = {"Erase Tag"};
  _items[5] = {"Advanced"};
  setItems(_items, 6);
}


void ChameleonMfcToolsScreen::_writeUid() {
  static const InputSelectAction::Option sources[] = {
    {"Manual", "manual"}, {"UID File", "uid"}, {"Dump", "dump"},
  };
  const char* r = InputSelectAction::popup("UID Source", sources, 3, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "manual") == 0) {
    String hex = InputTextAction::popup("UID (8 or 14 hex)", "", InputTextAction::INPUT_HEX);
    if (InputTextAction::wasCancelled()) { render(); return; }
    hex.replace(" ", ""); hex.replace(":", "");
    if (hex.length() != 8 && hex.length() != 14) { render(); ShowStatusAction::show("UID must be 4 or 7 bytes", 1600); return; }
    uint8_t uid[7] = {}; const uint8_t len = (uint8_t)(hex.length() / 2u);
    for (uint8_t i=0;i<len;++i) { char b[3]={hex[i*2],hex[i*2+1],0}; char* e=nullptr; unsigned long v=strtoul(b,&e,16); if(!e||*e){render();ShowStatusAction::show("Bad hex",1200);return;} uid[i]=(uint8_t)v; }
    Screen.push(new ChameleonMfcUidWriteScreen(uid, len));
    return;
  }
  if (strcmp(r, "uid") == 0) { _writeUidFromFile(); return; }
  _writeUidFromDump();
}

void ChameleonMfcToolsScreen::_writeUidFromFile() {
  uint8_t n = _browser.load(this, "/unigeek/nfc/uids", BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".uid"));
  if (!n) { render(); ShowStatusAction::show("No saved UIDs", 1600); render(); return; }
  static constexpr uint8_t kMax=10; const uint8_t count=n<kMax?n:kMax; InputSelectAction::Option opts[kMax]; String vals[kMax];
  for(uint8_t i=0;i<count;++i){vals[i]=String(i);opts[i]={_browser.entry(i).name.c_str(),vals[i].c_str()};}
  const char* r=InputSelectAction::popup("Saved UIDs",opts,count,nullptr); if(!r){render();return;} uint8_t i=(uint8_t)atoi(r); if(i>=count){render();return;}
  Screen.push(new ChameleonMfcUidWriteScreen(_browser.entry(i).path));
}

void ChameleonMfcToolsScreen::_writeUidFromDump() {
  uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin"));
  if (!n) { render(); ShowStatusAction::show("No saved dumps", 1600); render(); return; }
  static constexpr uint8_t kMax=10; const uint8_t count=n<kMax?n:kMax; InputSelectAction::Option opts[kMax]; String vals[kMax];
  for(uint8_t i=0;i<count;++i){vals[i]=String(i);opts[i]={_browser.entry(i).name.c_str(),vals[i].c_str()};}
  const char* r=InputSelectAction::popup("Saved Dumps",opts,count,nullptr); if(!r){render();return;} uint8_t i=(uint8_t)atoi(r); if(i>=count){render();return;}
  fs::File f=Uni.Storage->open(_browser.entry(i).path.c_str(),"r"); if(!f||f.size()==0||f.size()>4096){if(f)f.close();render();ShowStatusAction::show("Invalid dump",1600);return;}
  const size_t len=f.size(); uint8_t* dump=new uint8_t[len]; if(!dump){f.close();render();ShowStatusAction::show("Failed",1600);return;} const size_t got=f.read(dump,len); f.close();
  if(got!=len){delete[] dump;render();ShowStatusAction::show("Invalid dump",1600);return;} const auto info=NfcDumpParser::inspect(dump,len); delete[] dump;
  if(!NfcDumpParser::isMifareClassic(info.type)||!info.uidValid||(info.uidLen!=4&&info.uidLen!=7)){render();ShowStatusAction::show("Dump UID not supported",1600);return;}
  Screen.push(new ChameleonMfcUidWriteScreen(info.uid, info.uidLen));
}

void ChameleonMfcToolsScreen::_writeFromFile() {
  static constexpr uint8_t kMax = 10;
  uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 1024));
  if (!n) {
    render();
    ShowStatusAction::show("No Classic 1K .bin", 1600);
    render();
    return;
  }
  const uint8_t count = n < kMax ? n : kMax;
  InputSelectAction::Option opts[kMax];
  String vals[kMax];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("Classic 1K dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count) { render(); return; }
  String path = _browser.entry(idx).path;
  render();
  Screen.push(new ChameleonMfcWriteScreen(path));
}

void ChameleonMfcToolsScreen::_writeFromSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) {
    render();
    ShowStatusAction::show("Could not read slots", 1600);
    render();
    return;
  }

  InputSelectAction::Option opts[8];
  String labels[8];
  String vals[8];
  for (uint8_t i = 0; i < 8; ++i) {
    labels[i] = String("Slot ") + (i + 1) + " - " +
                ChameleonClient::tagTypeName(types[i].hfType);
    vals[i] = String(i);
    opts[i] = {labels[i].c_str(), vals[i].c_str()};
  }

  const char* r = InputSelectAction::popup("Source slot", opts, 8, nullptr);
  if (!r) { render(); return; }
  const uint8_t slot = (uint8_t)atoi(r);
  if (slot >= 8) { render(); return; }

  const uint16_t hfType = types[slot].hfType;
  if (hfType == 0) {
    render();
    ShowStatusAction::show("Empty slot", 1200);
    render();
    return;
  }
  if (!(hfType == 1001)) {
    render();
    ShowStatusAction::show("Tag not supported", 1600);
    render();
    return;
  }

  Screen.push(new ChameleonMfcWriteScreen(slot));
}

void ChameleonMfcToolsScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"From File", "file"},
    {"From Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write Dump to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}


void ChameleonMfcToolsScreen::_eraseTag() {
  Header header;
  header.render("Erase Tag");

  auto& c = ChameleonClient::get();

  uint8_t    previousMode = 0;
  const bool restoreMode  = c.getMode(&previousMode);
  c.setMode(1);

  auto&     lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

  uint8_t uid[7] = {}, uidLen = 0, atqa[2] = {}, sak = 0;
  if (!c.scan14A(uid, &uidLen, atqa, &sak) || sak != 0x08) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Tag must be MFC1K", 1600);
    render();
    return;
  }

  // Match PN532 Erase Tag semantics: preserve manufacturer block and every
  // sector trailer (keys/access bits), and clear data blocks only.
  uint8_t keysA[kMfc1kSectors][6] = {}, keysB[kMfc1kSectors][6] = {};
  bool    foundA[kMfc1kSectors]   = {}, foundB[kMfc1kSectors]   = {};

  ProgressView::init();
  bool keysOk = true;

  const uint16_t totalKeys = kMfc1kSectors * 2u;
  for (uint8_t sector = 0; sector < kMfc1kSectors; ++sector) {
    const uint8_t block = sector * 4u;
    char          msg[40];

    const uint16_t keyAIndex = sector * 2u + 1u;
    snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...", (unsigned)keyAIndex, (unsigned)totalKeys);
    ProgressView::progress(msg, (int)((uint32_t)(keyAIndex - 1u) * 100u / totalKeys));
    foundA[sector] = c.mf1CheckKeysOfBlock(block, 0x60, &kEraseKeys[0][0], kEraseKeyCount, keysA[sector]);

    const uint16_t keyBIndex = keyAIndex + 1u;
    snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...", (unsigned)keyBIndex, (unsigned)totalKeys);
    ProgressView::progress(msg, (int)((uint32_t)(keyBIndex - 1u) * 100u / totalKeys));
    foundB[sector] = c.mf1CheckKeysOfBlock(block, 0x61, &kEraseKeys[0][0], kEraseKeyCount, keysB[sector]);

    if (!foundA[sector] && !foundB[sector]) { keysOk = false; break; }
  }
  ProgressView::finish();

  if (!keysOk) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Failed: missing key", 1600);
    render();
    return;
  }

  static constexpr uint16_t kDataBlocks = 47;

  uint8_t  zero[16] = {};
  uint16_t erased   = 0;
  bool     ok       = true;

  ProgressView::init();
  for (uint8_t sector = 0; sector < kMfc1kSectors && ok; ++sector) {
    const uint8_t first = sector * 4u, trailer = _mfcTrailer(sector);

    for (uint8_t block = first; block < trailer; ++block) {
      if (block == 0) continue;   // manufacturer block stays untouched

      char msg[36];
      snprintf(msg, sizeof(msg), "Erasing blocks (%u/%u)...", (unsigned)(erased + 1u), (unsigned)kDataBlocks);
      ProgressView::progress(msg, (int)((uint32_t)erased * 100u / kDataBlocks));

      ok = _mfcWriteWithKnownKey(c, block, zero, keysA[sector], foundA[sector], keysB[sector], foundB[sector]);
      if (!ok) break;
      ++erased;
    }
  }

  const bool complete = ok && erased == kDataBlocks;
  if (complete) ProgressView::progress("Erase complete", 100);
  ProgressView::finish();

  if (restoreMode) c.setMode(previousMode);
  render();
  ShowStatusAction::show(complete ? "Tag erased" : "Failed", 1600);
  render();
}

void ChameleonMfcToolsScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMagicScreen());
  else if (index == 1) Screen.push(new ChameleonMfcScreen());
  else if (index == 2) _writeUid();
  else if (index == 3) _writeTag();
  else if (index == 4) _eraseTag();
  else if (index == 5) Screen.push(new ChameleonMfcAdvancedScreen());
}

void ChameleonMfcToolsScreen::onBack() { Screen.goBack(); }
