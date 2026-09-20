#include "IdentityToolsScreen.h"
#include <ctype.h>
#include <esp_system.h>
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/IdentityFile.h"

namespace {
String baseName(const String& p){ int s=p.lastIndexOf('/'); return s>=0?p.substring(s+1):p; }
String stem(String p){ p=baseName(p); int d=p.lastIndexOf('.'); if(d>0)p.remove(d); return p; }
String cleanName(String n,const char* ext){ n.trim(); String e(ext); if(n.endsWith(e))n.remove(n.length()-e.length()); for(int i=0;i<(int)n.length();++i){char c=n[i]; if(!(isalnum((unsigned char)c)||c=='-'||c=='_'))n.setCharAt(i,'_');} while(n.indexOf("__")>=0)n.replace("__","_"); return n; }
String uidHex(const uint8_t* u,size_t n){ return LFCodec::hex(u,n,false); }
bool parseHex(const String& s,uint8_t* out,size_t& n){ String h; for(size_t i=0;i<s.length();++i)if(isxdigit((unsigned char)s[i]))h+=s[i]; if(h.length()%2||h.length()>20)return false; n=h.length()/2; if(n!=4&&n!=7&&n!=10)return false; for(size_t i=0;i<n;++i){ char b[3]={h[i*2],h[i*2+1],0}; char* end=nullptr; out[i]=(uint8_t)strtoul(b,&end,16); if(!end||*end)return false;} return true; }
String uniquePath(const String& dir,const String& base,const char* ext){ String p=dir+"/"+base+ext; if(!Uni.Storage->exists(p.c_str()))return p; for(int i=2;i<1000;++i){String q=dir+"/"+base+"_("+i+")"+ext;if(!Uni.Storage->exists(q.c_str()))return q;} return ""; }
String fieldValue(const LFCodec::DecodedData& d,const LFCodec::FieldInfo& f){ switch(f.id){case LFCodec::FieldId::NumericId: case LFCodec::FieldId::RawData:return LFCodec::hex(d.data,d.length,false);case LFCodec::FieldId::FacilityCode:return String(d.facilityCode);case LFCodec::FieldId::CardNumber:return String(d.cardNumber);case LFCodec::FieldId::TextId:return d.hasTextId?String(d.textId):String();}return ""; }
}

const char* UidLibraryScreen::title(){return _state==BROWSE?"Saved UIDs":(_state==DETAILS?"UID Details":"UID Actions");}
void UidLibraryScreen::onInit(){_browser.root=_dir;open();}
void UidLibraryScreen::open(){_state=BROWSE;if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);Screen.goBack();return;}Uni.Storage->makeDir("/unigeek");Uni.Storage->makeDir("/unigeek/nfc");Uni.Storage->makeDir(_dir.c_str());_browser.root=_dir;setItems(_browser.items(),_browser.load(this,_browseDir,".uid",nullptr,BrowseFileView::STEM));}
void UidLibraryScreen::details(){_state=DETAILS;_d0=baseName(_path);_d1="ISO14443A";_d2=uidHex(_uid,_uidLen);_details[0]={"File",_d0};_details[1]={"Type",_d1};_details[2]={"UID",_d2};_details[3]={"[Press]","Actions"};_detailsView.resetScroll();_detailsView.setRows(_details,4);}
void UidLibraryScreen::onItemSelected(uint8_t i){if(_state==BROWSE){if(i>=_browser.count())return;auto e=_browser.entry(i);if(e.isDir){_browseDir=e.path;open();return;}if(!IdentityFile::loadNfcUid(e.path,_uid,sizeof(_uid),_uidLen)){ShowStatusAction::show("Invalid UID file",1600);return;}_path=e.path;details();}else if(_state==DETAILS){_state=ACTIONS;setItems(_actions);}else if(_state==ACTIONS){Screen.push(new UidFormScreen(i==0?UidFormScreen::COPY_ITEM:UidFormScreen::EDIT_ITEM,_path));}}
void UidLibraryScreen::onBack(){if(_state==ACTIONS){details();return;}if(_state==DETAILS){open();return;}Screen.goBack();}
void UidLibraryScreen::onUpdate(){if(_state!=DETAILS){ListScreen::onUpdate();return;}if(!Uni.Nav->wasPressed())return;auto d=Uni.Nav->readDirection();if(d==INavigation::DIR_BACK){open();return;}if(d==INavigation::DIR_PRESS){_state=ACTIONS;setItems(_actions);return;}_detailsView.onNav(d);}
void UidLibraryScreen::onRender(){if(_state==DETAILS){_detailsView.render(bodyX(),bodyY(),bodyW(),bodyH());return;}ListScreen::onRender();}

UidFormScreen::UidFormScreen(Mode m,const String& p):_mode(m),_path(p){}
void UidFormScreen::onInit(){if(_mode!=NEW_ITEM&&_path.length()){if(!IdentityFile::loadNfcUid(_path,_uid,sizeof(_uid),_uidLen)){ShowStatusAction::show("Invalid UID file",1600);Screen.goBack();return;}_source=MANUAL;if(_mode==COPY_ITEM)_path="";}else randomize();rebuild();}
void UidFormScreen::randomize(){for(size_t i=0;i<_uidLen;++i)_uid[i]=(uint8_t)esp_random();}
void UidFormScreen::rebuild(){_state=FORM;uint8_t sel=_selectedIndex;_count=0;_labels[_count]="ISO14443A";_items[_count]={"Type",_labels[_count].c_str()};++_count;if(_mode!=NEW_ITEM){_labels[_count]=_uidLen==4?"4 bytes":(_uidLen==7?"7 bytes":"10 bytes");_items[_count]={"UID Length",_labels[_count].c_str()};++_count;_labels[_count]=uidHex(_uid,_uidLen);_items[_count]={"UID Value",_labels[_count].c_str()};++_count;}else{_labels[_count]=_source==RANDOM?"Random":(_source==SAVED?"File":"Manual");_items[_count]={"UID",_labels[_count].c_str()};++_count;if(_source==SAVED){_labels[_count]=_savedPath.length()?stem(_savedPath):"-";_items[_count]={"UID File",_labels[_count].c_str()};++_count;}else{_labels[_count]=_uidLen==4?"4 bytes":(_uidLen==7?"7 bytes":"10 bytes");_items[_count]={"UID Length",_labels[_count].c_str()};++_count;if(_source==MANUAL){_labels[_count]=uidHex(_uid,_uidLen);_items[_count]={"UID Value",_labels[_count].c_str()};++_count;}}}_items[_count++]={"Save",nullptr};setItems(_items,_count,min<uint8_t>(sel,_count-1));}
void UidFormScreen::chooseSource(){_state=SOURCE_SELECT;setItems(_sourceItems,3,(uint8_t)_source);}
void UidFormScreen::chooseLength(){_state=LENGTH_SELECT;uint8_t s=_uidLen==4?0:(_uidLen==7?1:2);setItems(_lengthItems,3,s);}
void UidFormScreen::chooseFile(){if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);rebuild();return;}_state=FILE_SELECT;_browser.root="/unigeek/nfc/uids";setItems(_browser.items(),_browser.load(this,_pickDir,".uid",nullptr,BrowseFileView::STEM));}
void UidFormScreen::manual(){String v=InputTextAction::popup("UID",uidHex(_uid,_uidLen).c_str(),InputTextAction::INPUT_HEX);if(!InputTextAction::wasCancelled()){size_t n=0;uint8_t u[10];if(parseHex(v,u,n)){memcpy(_uid,u,n);_uidLen=n;}else ShowStatusAction::show("UID must be 4, 7 or 10 bytes",1600);}rebuild();}
void UidFormScreen::save(){if(_source==SAVED&&!_savedPath.length()){ShowStatusAction::show("Select UID file",1600);rebuild();return;}if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);return;}String p=_path;if(!p.length()){String name=InputTextAction::popup("Save UID",("UID_"+uidHex(_uid,_uidLen)).c_str());if(InputTextAction::wasCancelled()){rebuild();return;}String b=cleanName(name,".uid");if(!b.length()){ShowStatusAction::show("Invalid file name",1600);return;}Uni.Storage->makeDir("/unigeek");Uni.Storage->makeDir("/unigeek/nfc");Uni.Storage->makeDir("/unigeek/nfc/uids");p=uniquePath("/unigeek/nfc/uids",b,".uid");}if(p.length()&&IdentityFile::saveNfcUid(p,_uid,_uidLen)){_path=p;ShowStatusAction::show(("Saved: "+baseName(p)).c_str(),1600);}else ShowStatusAction::show("Failed",1600);rebuild();}
void UidFormScreen::onItemSelected(uint8_t i){if(_state==SOURCE_SELECT){_source=(Source)i;if(_source==RANDOM)randomize();if(_source==SAVED)_savedPath="";_selectedIndex=1;rebuild();return;}if(_state==LENGTH_SELECT){size_t next=i==0?4:(i==1?7:10);if(next!=_uidLen){_uidLen=next;if(_source==RANDOM)randomize();else if(_source==MANUAL){memset(_uid,0,sizeof(_uid));}}_selectedIndex=_mode==NEW_ITEM?2:1;rebuild();return;}if(_state==FILE_SELECT){if(i>=_browser.count())return;auto e=_browser.entry(i);if(e.isDir){_pickDir=e.path;chooseFile();return;}size_t n=0;if(IdentityFile::loadNfcUid(e.path,_uid,sizeof(_uid),n)){_uidLen=n;_savedPath=e.path;_selectedIndex=2;rebuild();}else ShowStatusAction::show("Invalid UID file",1600);return;}uint8_t row=0;if(i==row++)return;if(_mode!=NEW_ITEM){if(i==row++){chooseLength();return;}if(i==row++){manual();return;}if(i==row)save();return;}if(i==row++){chooseSource();return;}if(_source==SAVED){if(i==row++){chooseFile();return;}}else{if(i==row++){chooseLength();return;}if(_source==MANUAL&&i==row++){manual();return;}}if(i==row)save();}
void UidFormScreen::onBack(){if(_state!=FORM){rebuild();return;}Screen.goBack();}

const char* IdLibraryScreen::title(){return _state==BROWSE?"Saved IDs":(_state==DETAILS?"ID Details":"ID Actions");}
void IdLibraryScreen::onInit(){_browser.root=_dir;open();}
void IdLibraryScreen::open(){_state=BROWSE;if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);Screen.goBack();return;}Uni.Storage->makeDir("/unigeek");Uni.Storage->makeDir("/unigeek/rfid");Uni.Storage->makeDir(_dir.c_str());_browser.root=_dir;setItems(_browser.items(),_browser.load(this,_browseDir,".id",nullptr,BrowseFileView::STEM));}
void IdLibraryScreen::details(){_state=DETAILS;_dc=0;auto add=[&](String l,String v){_dl[_dc]=l;_dv[_dc]=v;_details[_dc]={_dl[_dc].c_str(),_dv[_dc]};++_dc;};add("File",baseName(_path));auto info=LFCodec::format(_data.protocol);add("Type",info?info->name:"Unknown");LFCodec::Field f[5];size_t n=LFCodec::fields(_data,f,5);for(size_t i=0;i<n&&_dc<8;++i)add(f[i].label,f[i].value);_details[_dc++]={"[Press]","Actions"};_detailsView.resetScroll();_detailsView.setRows(_details,_dc);}
void IdLibraryScreen::onItemSelected(uint8_t i){if(_state==BROWSE){if(i>=_browser.count())return;auto e=_browser.entry(i);if(e.isDir){_browseDir=e.path;open();return;}if(!IdentityFile::loadLfId(e.path,_data)){ShowStatusAction::show("Invalid ID file",1600);return;}_path=e.path;details();}else if(_state==DETAILS){_state=ACTIONS;setItems(_actions);}else Screen.push(new IdFormScreen(i==0?IdFormScreen::COPY_ITEM:IdFormScreen::EDIT_ITEM,_path));}
void IdLibraryScreen::onBack(){if(_state==ACTIONS){details();return;}if(_state==DETAILS){open();return;}Screen.goBack();}
void IdLibraryScreen::onUpdate(){if(_state!=DETAILS){ListScreen::onUpdate();return;}if(!Uni.Nav->wasPressed())return;auto d=Uni.Nav->readDirection();if(d==INavigation::DIR_BACK){open();return;}if(d==INavigation::DIR_PRESS){_state=ACTIONS;setItems(_actions);return;}_detailsView.onNav(d);}
void IdLibraryScreen::onRender(){if(_state==DETAILS){_detailsView.render(bodyX(),bodyY(),bodyW(),bodyH());return;}ListScreen::onRender();}

IdFormScreen::IdFormScreen(Mode m,const String&p):_mode(m),_path(p){}
void IdFormScreen::onInit(){if(_mode!=NEW_ITEM&&_path.length()){if(!IdentityFile::loadLfId(_path,_data)){ShowStatusAction::show("Invalid ID file",1600);Screen.goBack();return;}_source=MANUAL;if(_mode==COPY_ITEM)_path="";}else{auto f=LFCodec::formatAt(0);if(f)LFCodec::create(f->protocol,_data);randomize();}rebuild();}
void IdFormScreen::randomize(){
  if(_data.protocol==LFCodec::Protocol::Unknown)return;
  auto info=LFCodec::format(_data.protocol); if(!info)return;
  LFCodec::DecodedData d; if(!LFCodec::create(_data.protocol,d))return;
  switch(_data.protocol){
    case LFCodec::Protocol::EM410X: {
      uint8_t raw[5]; for(auto &b:raw)b=(uint8_t)esp_random();
      LFCodec::setField(d,LFCodec::FieldId::NumericId,LFCodec::hex(raw,sizeof(raw),false)); break;
    }
    case LFCodec::Protocol::IoProx:
      LFCodec::setField(d,LFCodec::FieldId::FacilityCode,String((uint8_t)esp_random()));
      LFCodec::setField(d,LFCodec::FieldId::CardNumber,String((uint16_t)esp_random())); break;
    case LFCodec::Protocol::PACStanley: {
      static const char chars[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; String id; id.reserve(8);
      for(uint8_t i=0;i<8;++i)id+=chars[esp_random()%(sizeof(chars)-1)];
      LFCodec::setField(d,LFCodec::FieldId::TextId,id); break;
    }
    case LFCodec::Protocol::HIDProx:
    case LFCodec::Protocol::Viking:
    case LFCodec::Protocol::Jablotron:
      return;
    default:return;
  }
  _data=d;
}
void IdFormScreen::rebuild(){_state=FORM;uint8_t sel=_selectedIndex;_count=0;auto info=LFCodec::format(_data.protocol);if(_mode!=NEW_ITEM){_labels[_count]=info?info->name:"Unknown";_items[_count]={"Type",_labels[_count].c_str()};++_count;size_t n=LFCodec::editableFieldCount(_data.protocol);for(size_t i=0;i<n&&_count<9;++i){auto f=LFCodec::editableFieldAt(_data.protocol,i);_labels[_count]=fieldValue(_data,*f);_items[_count]={f->label,_labels[_count].c_str()};++_count;}}else{if(_source!=SAVED){_labels[_count]=info?info->name:"Select...";_items[_count]={"Type",_labels[_count].c_str()};++_count;}_labels[_count]=_source==RANDOM?"Random":(_source==SAVED?"File":"Manual");_items[_count]={"ID",_labels[_count].c_str()};++_count;if(_source==SAVED){_labels[_count]=_savedPath.length()?stem(_savedPath):"-";_items[_count]={"ID File",_labels[_count].c_str()};++_count;if(_savedPath.length()){_labels[_count]=info?info->name:"Unknown";_items[_count]={"Type",_labels[_count].c_str()};++_count;}}else if(_source==MANUAL){size_t n=LFCodec::editableFieldCount(_data.protocol);for(size_t i=0;i<n&&_count<9;++i){auto f=LFCodec::editableFieldAt(_data.protocol,i);_labels[_count]=fieldValue(_data,*f);_items[_count]={f->label,_labels[_count].c_str()};++_count;}}}_items[_count++]={"Save",nullptr};setItems(_items,_count,min<uint8_t>(sel,_count-1));}
void IdFormScreen::types(){_state=TYPE_SELECT;_typeCount=min<size_t>(LFCodec::formatCount(),8);for(uint8_t i=0;i<_typeCount;++i){auto f=LFCodec::formatAt(i);_types[i]={f?f->name:"Unknown"};}setItems(_types,_typeCount);}
void IdFormScreen::sources(){_state=SOURCE_SELECT;auto info=LFCodec::format(_data.protocol);bool canRandom=info&&(info->capabilities&LFCodec::CanGenerateRandom);if(canRandom)setItems(_sources,3,(uint8_t)_source);else{if(_source==RANDOM)_source=MANUAL;setItems(_sources+1,2,(uint8_t)_source-1);}}
void IdFormScreen::files(){if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);rebuild();return;}_state=FILE_SELECT;_browser.root="/unigeek/rfid/ids";setItems(_browser.items(),_browser.load(this,_pickDir,".id",nullptr,BrowseFileView::STEM));}
void IdFormScreen::editField(size_t i){auto f=LFCodec::editableFieldAt(_data.protocol,i);if(!f)return;auto mode=f->type==LFCodec::FieldType::Hex?InputTextAction::INPUT_HEX:InputTextAction::INPUT_TEXT;String v=InputTextAction::popup(f->label,fieldValue(_data,*f).c_str(),mode);if(!InputTextAction::wasCancelled()&&!LFCodec::setField(_data,f->id,v))ShowStatusAction::show("Invalid value",1600);rebuild();}
void IdFormScreen::save(){if(_source==SAVED&&!_savedPath.length()){ShowStatusAction::show("Select ID file",1600);rebuild();return;}if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);return;}auto info=LFCodec::format(_data.protocol);if(!info)return;uint8_t raw[LFCodec::kMaxDataSize]={};if(!LFCodec::isComplete(_data)||!LFCodec::encode(_data,raw,sizeof(raw))){ShowStatusAction::show("Incomplete ID",1600);return;}String p=_path;if(!p.length()){String suggested=String(info->filePrefix)+"_"+LFCodec::hex(raw,_data.length,false);String n=InputTextAction::popup("Save ID",suggested.c_str());if(InputTextAction::wasCancelled()){rebuild();return;}String b=cleanName(n,".id");if(!b.length()){ShowStatusAction::show("Invalid file name",1600);return;}Uni.Storage->makeDir("/unigeek");Uni.Storage->makeDir("/unigeek/rfid");Uni.Storage->makeDir("/unigeek/rfid/ids");p=uniquePath("/unigeek/rfid/ids",b,".id");}if(p.length()&&IdentityFile::saveLfId(p,_data.protocol,raw,_data.length)){_path=p;memcpy(_data.data,raw,_data.length);ShowStatusAction::show(("Saved: "+baseName(p)).c_str(),1600);}else ShowStatusAction::show("Failed",1600);rebuild();}
void IdFormScreen::onItemSelected(uint8_t i){if(_state==TYPE_SELECT){auto f=LFCodec::formatAt(i);if(f){LFCodec::create(f->protocol,_data);if(_source==RANDOM){if(f->capabilities&LFCodec::CanGenerateRandom)randomize();else _source=MANUAL;}}_selectedIndex=0;rebuild();return;}if(_state==SOURCE_SELECT){auto info=LFCodec::format(_data.protocol);bool canRandom=info&&(info->capabilities&LFCodec::CanGenerateRandom);_source=(Source)(canRandom?i:i+1);if(_source==RANDOM)randomize();if(_source==SAVED)_savedPath="";_selectedIndex=1;rebuild();return;}if(_state==FILE_SELECT){if(i>=_browser.count())return;auto e=_browser.entry(i);if(e.isDir){_pickDir=e.path;files();return;}LFCodec::DecodedData d;if(IdentityFile::loadLfId(e.path,d)){_data=d;_savedPath=e.path;_selectedIndex=2;rebuild();}else ShowStatusAction::show("Invalid ID file",1600);return;}uint8_t row=0;if(_mode!=NEW_ITEM){if(i==row++)return;size_t n=LFCodec::editableFieldCount(_data.protocol);if(i>=row&&i<row+n){editField(i-row);return;}row+=n;if(i==row)save();return;}if(_source!=SAVED){if(i==row++){types();return;}}if(i==row++){sources();return;}if(_source==SAVED){if(i==row++){files();return;}if(_savedPath.length()&&i==row++)return;}else if(_source==MANUAL){size_t n=LFCodec::editableFieldCount(_data.protocol);if(i>=row&&i<row+n){editField(i-row);return;}row+=n;}if(i==row)save();}
void IdFormScreen::onBack(){if(_state!=FORM){rebuild();return;}Screen.goBack();}
