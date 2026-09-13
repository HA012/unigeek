#include "ChameleonMfcAdvancedScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/views/ProgressView.h"

namespace {
static void renderTagPrompt(const char* message, int bx, int by, int bw, int bh) {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString(message, bx + bw / 2, by + bh / 2);
}
static const uint8_t kKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},{0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},{0,0,0,0,0,0},{0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},{0x1A,0x98,0x2C,0x7E,0x45,0x9A},{0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}
};
static uint8_t sectorForBlock(uint16_t b){ return b<128 ? b/4 : 32+(b-128)/16; }
static bool findKey(ChameleonClient& c,uint8_t block,uint8_t out[6],uint8_t& type){
  const uint8_t count=(uint8_t)(sizeof(kKeys)/sizeof(kKeys[0]));
  if(c.mf1CheckKeysOfBlock(block,0x60,&kKeys[0][0],count,out)){type=0x60;return true;}
  if(c.mf1CheckKeysOfBlock(block,0x61,&kKeys[0][0],count,out)){type=0x61;return true;}
  return false;
}
static String hex8(const uint8_t* d){ String s; char b[3]; for(int i=0;i<8;++i){snprintf(b,sizeof(b),"%02X",d[i]);s+=b;} return s; }
}

void ChameleonMfcAdvancedScreen::onInit(){
  _items[0] = {"Read Memory"};
  _items[1] = {"Edit Memory"};
  _items[2] = {"Edit UID (Gen1A and Gen3)"};
  _items[3] = {"Lock UID (Gen3)"};
  setItems(_items);
}
void ChameleonMfcAdvancedScreen::_addRow(const String& l,const String& v){ if(_rowCount>=MAX_ROWS)return; _labels[_rowCount]=l;_values[_rowCount]=v;_rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount].c_str()};++_rowCount; }
void ChameleonMfcAdvancedScreen::onItemSelected(uint8_t i){
  if (i == 0) _readMemory();
  else if (i == 1) _editMemory();
  else if (i == 2) _editUid();
  else if (i == 3) _lockUidGen3();
}
void ChameleonMfcAdvancedScreen::onBack(){ if(_showingMemory){_showingMemory=false;setItems(_items);render();} else Screen.goBack(); }
void ChameleonMfcAdvancedScreen::onUpdate(){ if(!_showingMemory){ListScreen::onUpdate();return;} if(Uni.Nav->wasPressed()){auto d=Uni.Nav->readDirection();if(d==INavigation::DIR_BACK)onBack();else _view.onNav(d);} }
void ChameleonMfcAdvancedScreen::onRender(){ if(_showingMemory){_view.render(bodyX(),bodyY(),bodyW(),bodyH());return;} ListScreen::onRender(); }

void ChameleonMfcAdvancedScreen::_readMemory(){
  Header header; header.render("Read Memory");
  auto& c=ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };
  uint8_t uid[7]={},ul=0,atqa[2]={},sak=0;
  if(!c.scan14A(uid,&ul,atqa,&sak)||(sak!=0x08&&sak!=0x18)){restore();render();ShowStatusAction::show("Tag not MIFARE Classic");render();return;}
  const uint16_t blocks=sak==0x18?256:64; uint8_t keys[40][6]={},types[40]={}; bool have[40]={};
  _rowCount=0; _addRow("Type",sak==0x18?"MF Classic 4K":"MF Classic 1K");
  String uidText; for(uint8_t i=0;i<ul;++i){char b[4];snprintf(b,sizeof(b),"%s%02X",i?":":"",uid[i]);uidText+=b;} _addRow("UID",uidText);
  _addRow("Blocks", String(blocks));
  ProgressView::init();
  for(uint16_t b=0;b<blocks;++b){
    uint8_t sec=sectorForBlock(b); if(!have[sec])have[sec]=findKey(c,(uint8_t)b,keys[sec],types[sec]);
    uint8_t data[16]={}; bool ok=have[sec]&&c.mf1ReadBlock((uint8_t)b,types[sec],keys[sec],data);
    char msg[36];snprintf(msg,sizeof(msg),"Reading blocks (%u/%u)...",(unsigned)(b+1),(unsigned)blocks);ProgressView::progress(msg,(int)((uint32_t)b*100u/blocks));
    if(ok){
      _addRow("B"+String(b)+" 0-7",hex8(data));
      _addRow("B"+String(b)+" 8-F",hex8(data+8));
    } else {
      _addRow("B"+String(b),"Unreadable (key)");
    }
  }
  ProgressView::finish(); restore(); _view.resetScroll(); _view.setRows(_rows,_rowCount); _showingMemory=true; render();
}

void ChameleonMfcAdvancedScreen::_editMemory(){
  Header header; header.render("Edit Memory");
  auto& c=ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };
  uint8_t uid[7]={},ul=0,atqa[2]={},sak=0;
  if(!c.scan14A(uid,&ul,atqa,&sak)||(sak!=0x08&&sak!=0x18)){restore();render();ShowStatusAction::show("Tag not MIFARE Classic");render();return;}
  const int max=sak==0x18?255:63; int block=InputNumberAction::popup((String("Block (1..")+String(max)+")").c_str(),1,max,1); if(InputNumberAction::wasCancelled()){restore();render();return;}
  String h=InputTextAction::popup("Block data (32 hex)","",InputTextAction::INPUT_HEX); if(InputTextAction::wasCancelled()){restore();render();return;} h.replace(" ","");h.replace(":","");
  if(h.length()!=32){restore();render();ShowStatusAction::show("Need 32 hex chars");render();return;} uint8_t d[16]={}; for(int i=0;i<16;++i){char x[3]={h[i*2],h[i*2+1],0};char*e=nullptr;unsigned long v=strtoul(x,&e,16);if(!e||*e){restore();render();ShowStatusAction::show("Bad hex");render();return;}d[i]=(uint8_t)v;}
  uint8_t key[6]={},type=0; bool ok=findKey(c,(uint8_t)block,key,type)&&c.mf1WriteBlock((uint8_t)block,type,key,d); restore(); render();ShowStatusAction::show(ok?"Block written":"Write failed: missing key",1600);render();
}


void ChameleonMfcAdvancedScreen::_editUid(){
  Header header;
  header.render("Edit UID");
  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);
  auto restore = [&](){ if (restoreMode) c.setMode(previousMode); };

  // Match the PN532 tag-placement UX.
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  renderTagPrompt("Place tag on reader...", bx, by, bw, bh);

  uint8_t currentUid[7] = {}, currentUidLen = 0, atqa[2] = {}, sak = 0;
  if (!c.scan14A(currentUid, &currentUidLen, atqa, &sak)) {
    restore(); render(); ShowStatusAction::show("No tag detected"); render(); return;
  }

  const MagicCardType magic = c.detectMagicType();
  if (magic != MagicCardType::GEN1A && magic != MagicCardType::GEN3) {
    restore(); render();
    ShowStatusAction::show("Failed: Tag is not Gen1A or Gen3", 1800);
    render();
    return;
  }

  // detectMagicType() reselects internally; refresh the displayed UID after it.
  memset(currentUid, 0, sizeof(currentUid));
  currentUidLen = 0;
  if (!c.scan14A(currentUid, &currentUidLen, atqa, &sak) ||
      (currentUidLen != 4 && currentUidLen != 7)) {
    restore(); render(); ShowStatusAction::show("Edit UID failed"); render(); return;
  }

  uint8_t block0[16] = {};
  if (magic == MagicCardType::GEN1A) {
    if (currentUidLen != 4) {
      restore(); render(); ShowStatusAction::show("Edit UID failed"); render(); return;
    }

    // Open the Gen1A backdoor, then read block 0 so only UID+BCC are replaced.
    uint8_t resp[32] = {};
    uint16_t respLen = 0, st = 0;
    uint8_t halt[2] = {0x50, 0x00};
    (void)c.hf14ARaw(64 | 32 | 8, 200, 16, halt, sizeof(halt),
                     resp, &respLen, sizeof(resp), &st);
    uint8_t wake = 0x40;
    respLen = 0; st = 0;
    bool ok = c.hf14ARaw(128 | 64 | 8, 250, 7, &wake, 1,
                         resp, &respLen, sizeof(resp), &st) &&
              (st == 0 || st == 0x68) && respLen >= 1 && resp[0] == 0x0A;
    if (ok) {
      uint8_t unlock = 0x43;
      respLen = 0; st = 0;
      ok = c.hf14ARaw(64 | 8, 250, 8, &unlock, 1,
                      resp, &respLen, sizeof(resp), &st) &&
           (st == 0 || st == 0x68) && respLen >= 1 && resp[0] == 0x0A;
    }
    if (ok) {
      uint8_t read0[2] = {0x30, 0x00};
      respLen = 0; st = 0;
      ok = c.hf14ARaw(64 | 32 | 16 | 8, 500, 16, read0, sizeof(read0),
                      resp, &respLen, sizeof(resp), &st) &&
           (st == 0 || st == 0x68) && respLen >= 16;
      if (ok) memcpy(block0, resp, 16);
    }
    // Re-select without overwriting currentUid: block0 belongs to the UID
    // captured above, so a card swap at this point must abort the operation.
    uint8_t reselectUid[7] = {}, reselectUidLen = 0, reselectAtqa[2] = {}, reselectSak = 0;
    const bool sameTag = c.scan14A(reselectUid, &reselectUidLen, reselectAtqa, &reselectSak) &&
                         reselectUidLen == currentUidLen &&
                         memcmp(reselectUid, currentUid, currentUidLen) == 0;
    if (!ok || !sameTag) {
      restore(); render(); ShowStatusAction::show("Edit UID failed"); render(); return;
    }
  }

  auto uidHex = [](const uint8_t* uid, uint8_t len, bool colon) -> String {
    String out;
    char b[3];
    for (uint8_t i = 0; i < len; ++i) {
      if (colon && i) out += ':';
      snprintf(b, sizeof(b), "%02X", uid[i]);
      out += b;
    }
    return out;
  };

  const String initial = uidHex(currentUid, currentUidLen, false);
  String hex = InputTextAction::popup("New UID (8 or 14 hex)", initial.c_str(),
                                      InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { restore(); render(); return; }
  hex.replace(" ", "");
  hex.replace(":", "");
  if (hex.length() != 8 && hex.length() != 14) {
    restore(); render(); ShowStatusAction::show("UID must be 4 or 7 bytes", 1500); render(); return;
  }

  const uint8_t newUidLen = (uint8_t)(hex.length() / 2);
  if (magic == MagicCardType::GEN1A && newUidLen != 4) {
    restore(); render(); ShowStatusAction::show("Gen1A UID must be 4 bytes", 1600); render(); return;
  }

  uint8_t newUid[7] = {};
  for (uint8_t i = 0; i < newUidLen; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* end = nullptr;
    const unsigned long v = strtoul(b, &end, 16);
    if (!end || *end) {
      restore(); render(); ShowStatusAction::show("Bad hex", 1200); render(); return;
    }
    newUid[i] = (uint8_t)v;
  }

  // Final preview/confirmation. Back cancels; Press performs the write.
  // InputTextAction may leave pixels outside the body rectangle. Clear the
  // whole display before drawing the preview to avoid stale popup artefacts.
  lcd.fillRect(bodyX(), 0, lcd.width() - bodyX(), lcd.height(), TFT_BLACK);
  header.render("Edit UID");
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.drawString("Current UID", bx + 4, by + 8);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(uidHex(currentUid, currentUidLen, true), bx + 4, by + 24);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.drawString("New UID", bx + 4, by + 48);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(uidHex(newUid, newUidLen, true), bx + 4, by + 64);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("[Press] Write", bx + 4, by + bh - 18);

  while (true) {
    Uni.update();
    if (!Uni.Nav->wasPressed()) { delay(10); continue; }
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { restore(); render(); return; }
    if (dir == INavigation::DIR_PRESS) break;
  }

  // Revalidate the physical tag immediately before writing. For Gen1A, the
  // saved block0 must only ever be written back to the card it came from.
  uint8_t verifyUid[7] = {}, verifyUidLen = 0, verifyAtqa[2] = {}, verifySak = 0;
  if (!c.scan14A(verifyUid, &verifyUidLen, verifyAtqa, &verifySak) ||
      verifyUidLen != currentUidLen ||
      memcmp(verifyUid, currentUid, currentUidLen) != 0 ||
      c.detectMagicType() != magic) {
    restore(); render();
    ShowStatusAction::show("Edit UID failed", 1600);
    render();
    return;
  }

  const bool ok = c.writeMagicUid(magic, newUid, newUidLen, block0);
  restore(); render();
  ShowStatusAction::show(ok ? "UID edited" : "Edit UID failed", 1600);
  render();
}

void ChameleonMfcAdvancedScreen::_lockUidGen3(){
  Header header; header.render("Lock UID");
  auto& c = ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };

  if (c.detectMagicType() != MagicCardType::GEN3) {
    restore(); render(); ShowStatusAction::show("Tag is not Gen3", 1500); render(); return;
  }

  static const InputSelectAction::Option opts[] = {{"Lock UID permanently", "lock"}};
  const char* choice = InputSelectAction::popup("Permanent UID lock", opts, 1, nullptr);
  render();
  if (!choice || strcmp(choice, "lock") != 0) { restore(); return; }

  const uint8_t cmd[] = {0x90, 0xFD, 0x11, 0x11, 0x00};
  uint8_t resp[16] = {};
  uint16_t respLen = 0;
  uint16_t st = 0;
  const bool ok = c.hf14ARaw(128 | 64 | 32 | 16 | 8, 500,
                              sizeof(cmd) * 8u, cmd, sizeof(cmd),
                              resp, &respLen, sizeof(resp), &st) &&
                  (st == 0 || st == 0x68);
  restore(); ShowStatusAction::show(ok ? "Gen3 UID locked" : "Lock failed", 1600); render();
}
