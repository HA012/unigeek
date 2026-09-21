#include "NdefToolsScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/NdefBuilder.h"
#include "utils/nfc/NdefParser.h"

namespace {
constexpr size_t MAXN=491;
String bn(const String&p){int s=p.lastIndexOf('/');return s>=0?p.substring(s+1):p;}
String stem(String p){p=bn(p);if(p.endsWith(".ndef"))p.remove(p.length()-5);return p;}
String clean(String n){n.trim();if(n.endsWith(".ndef"))n.remove(n.length()-5);for(int i=0;i<(int)n.length();++i){char c=n[i];bool ok=isAlphaNumeric(c)||c=='-'||c=='_';if(!ok)n.setCharAt(i,'_');}while(n.indexOf("__")>=0)n.replace("__","_");while(n.startsWith("_"))n.remove(0,1);while(n.endsWith("_"))n.remove(n.length()-1);return n;}
String uniquePath(const String&dir,const String&base){String p=dir+"/"+base+".ndef";if(!Uni.Storage->exists(p.c_str()))return p;for(int n=2;n<1000;++n){p=dir+"/"+base+"_("+String(n)+").ndef";if(!Uni.Storage->exists(p.c_str()))return p;}return "";}
bool isSingleRecordMessage(const uint8_t* n, size_t len) {
  if (!n || len < 3) return false;
  size_t p = 0;
  const uint8_t h = n[p++];
  if (!(h & 0x80) || !(h & 0x40) || (h & 0x20)) return false; // MB + ME, no chunking
  const bool sr = (h & 0x10) != 0, il = (h & 0x08) != 0;
  if (p >= len) return false;
  const uint8_t typeLen = n[p++];
  uint32_t payloadLen = 0;
  if (sr) { if (p >= len) return false; payloadLen = n[p++]; }
  else { if (p + 4 > len) return false; payloadLen = ((uint32_t)n[p] << 24) | ((uint32_t)n[p+1] << 16) | ((uint32_t)n[p+2] << 8) | n[p+3]; p += 4; }
  uint8_t idLen = 0;
  if (il) { if (p >= len) return false; idLen = n[p++]; }
  return p + typeLen + idLen + payloadLen == len;
}
bool supportedEditable(const NdefParser::Result& r) {
  return r.valid && r.kind >= NdefParser::RECORD_TEXT && r.kind <= NdefParser::RECORD_VCARD &&
         !(r.kind == NdefParser::RECORD_TEXT && r.encoding == "UTF-16");
}

}

NdefRecordFormScreen::NdefRecordFormScreen(Mode m,const String&p,SaveCallback cb,void*ctx):_mode(m),_path(p),_cb(cb),_ctx(ctx){}
bool NdefRecordFormScreen::isValidSingleRecord(const uint8_t*b,size_t n){NdefParser::Result r;return isSingleRecordMessage(b,n)&&NdefParser::parse(b,n,r)&&supportedEditable(r);}
void NdefRecordFormScreen::onInit(){if((_mode==EDIT_FILE||_mode==COPY_FILE)&&!load(_path)){ShowStatusAction::show("Invalid NDEF record",1600);Screen.goBack();return;}if(_mode==COPY_FILE)_path="";rebuild();}
bool NdefRecordFormScreen::load(const String&p){if(!Uni.Storage||!Uni.Storage->isAvailable())return false;fs::File f=Uni.Storage->open(p.c_str(),"r");if(!f||!f.size()||f.size()>MAXN){if(f)f.close();return false;}uint8_t b[MAXN];size_t n=f.read(b,f.size());f.close();return parse(b,n);}
bool NdefRecordFormScreen::parse(const uint8_t*b,size_t n){_text=_url=_phone=_email=_name=_company=_address=_vphone=_vemail=_website="";NdefParser::Result r;if(!isSingleRecordMessage(b,n)||!NdefParser::parse(b,n,r)||!supportedEditable(r))return false;switch(r.kind){case NdefParser::RECORD_TEXT:_type=TEXT;_text=r.text;break;case NdefParser::RECORD_URL:_type=URL;_url=r.uri;break;case NdefParser::RECORD_PHONE:_type=PHONE;_phone=r.phone;break;case NdefParser::RECORD_EMAIL:_type=EMAIL;_email=r.email;break;case NdefParser::RECORD_VCARD:_type=VCARD;_name=r.contact;_company=r.company;_address=r.address;_vphone=r.phone;_vemail=r.email;_website=r.website;break;default:return false;}return true;}
void NdefRecordFormScreen::rebuild(){_preview=false;_state=FORM;uint8_t sel=_selectedIndex;_count=0;auto add=[&](const char*l,const String&v){_vals[_count]=v.length()?v:"-";_items[_count]={l,_vals[_count].c_str()};++_count;};static const char*tn[]={"Text","URL","Phone","Email","vCard"};add("Type",tn[_type]);if(_type==TEXT)add("Text",_text);else if(_type==URL)add("URL",_url);else if(_type==PHONE)add("Phone",_phone);else if(_type==EMAIL)add("Email",_email);else{add("Name",_name);add("Company",_company);add("Address",_address);add("Phone",_vphone);add("Email",_vemail);add("Website",_website);}if(_mode!=NEW_FILE)_items[_count++]={"Preview",nullptr};_items[_count++]={"Save",nullptr};setItems(_items,_count,min<uint8_t>(sel,_count-1));}
void NdefRecordFormScreen::selectType(){_state=TYPE_SELECT;setItems(_types,5);}
void NdefRecordFormScreen::editField(uint8_t i){String*v=nullptr;const char*title="Value";InputTextAction::Mode mode=InputTextAction::INPUT_TEXT;if(_type==TEXT){v=&_text;title="Text";}else if(_type==URL){v=&_url;title="URL";}else if(_type==PHONE){v=&_phone;title="Phone";mode=InputTextAction::INPUT_PHONE;}else if(_type==EMAIL){v=&_email;title="Email";}else{String*vs[]={&_name,&_company,&_address,&_vphone,&_vemail,&_website};const char*ts[]={"Name","Company","Address","Phone","Email","Website"};v=vs[i];title=ts[i];if(i==3)mode=InputTextAction::INPUT_PHONE;}String x=InputTextAction::popup(title,v->c_str(),mode);if(!InputTextAction::wasCancelled())*v=x;rebuild();}
bool NdefRecordFormScreen::complete()const{if(_type==TEXT)return _text.length();if(_type==URL)return _url.length();if(_type==PHONE)return _phone.length();if(_type==EMAIL)return _email.length();return _name.length();}
bool NdefRecordFormScreen::build(uint8_t*out,size_t&n){n=0;if(!complete())return false;if(_type==TEXT)return NdefBuilder::buildText(_text,out,n,MAXN);if(_type==URL)return NdefBuilder::buildUrl(_url,out,n,MAXN);if(_type==PHONE)return NdefBuilder::buildPhone(_phone,out,n,MAXN);if(_type==EMAIL)return NdefBuilder::buildEmail(_email,out,n,MAXN);return NdefBuilder::buildVcard(_name,_company,_address,_vphone,_vemail,_website,out,n,MAXN);}
void NdefRecordFormScreen::preview(){uint8_t b[MAXN];size_t n=0;if(!build(b,n)){rebuild();ShowStatusAction::show(complete()?"NDEF too large":"Complete required fields",1600);return;}_preview=true;_rc=0;auto row=[&](const String&l,const String&v){if(_rc>=32)return;_rl[_rc]=l;_rv[_rc]=v;_rows[_rc]={_rl[_rc].c_str(),_rv[_rc].c_str()};++_rc;};static const char*tn[]={"Text","URL","Phone","Email","vCard"};row("Type",tn[_type]);if(_type==TEXT)row("Text",_text);else if(_type==URL)row("URL",_url);else if(_type==PHONE)row("Phone",_phone);else if(_type==EMAIL)row("Email",_email);else{row("Name",_name);row("Company",_company);row("Address",_address);row("Phone",_vphone);row("Email",_vemail);row("Website",_website);}row("Size",String((unsigned)n)+" bytes");_view.resetScroll();_view.setRows(_rows,_rc);render();}
String NdefRecordFormScreen::suggested()const{if(_type==VCARD)return _name.length()?clean(_name):"vcard";if(_type==URL)return"url";if(_type==PHONE)return"phone";if(_type==EMAIL)return"email";return"text";}
void NdefRecordFormScreen::save(){uint8_t b[MAXN];size_t n=0;if(!build(b,n)){rebuild();ShowStatusAction::show(complete()?"NDEF too large":"Complete required fields",1600);return;}if(_mode==BUFFER){if(_cb&&_cb(_ctx,b,n)){ShowStatusAction::show("NDEF updated",1200);Screen.goBack();}else{rebuild();ShowStatusAction::show("Failed",1600);}return;}if(!Uni.Storage||!Uni.Storage->isAvailable()){rebuild();ShowStatusAction::show("Storage unavailable",1600);return;}String p=_path;if(!p.length()){String nm=InputTextAction::popup("Save NDEF",suggested().c_str());if(InputTextAction::wasCancelled()){rebuild();return;}String base=clean(nm);if(!base.length()){rebuild();ShowStatusAction::show("Invalid file name",1600);return;}Uni.Storage->makeDir("/unigeek/nfc");Uni.Storage->makeDir("/unigeek/nfc/ndefs");p=uniquePath("/unigeek/nfc/ndefs",base);}fs::File f=Uni.Storage->open(p.c_str(),"w");bool ok=f&&f.write(b,n)==n;if(f)f.close();if(ok)_path=p;rebuild();ShowStatusAction::show(ok?("Saved: "+bn(p)).c_str():"Failed",1600);}
void NdefRecordFormScreen::onItemSelected(uint8_t i){if(_state==TYPE_SELECT){_type=(Type)i;_text=_url=_phone=_email=_name=_company=_address=_vphone=_vemail=_website="";_selectedIndex=0;rebuild();return;}uint8_t r=0;if(i==r++){if(_mode==NEW_FILE||_mode==BUFFER)selectType();return;}uint8_t fields=_type==VCARD?6:1;if(i>=r&&i<r+fields){editField(i-r);return;}r+=fields;if(_mode!=NEW_FILE){if(i==r++){preview();return;}}if(i==r)save();}
void NdefRecordFormScreen::onBack(){if(_preview){rebuild();return;}if(_state!=FORM){rebuild();return;}Screen.goBack();}
void NdefRecordFormScreen::onUpdate(){if(_preview){if(Uni.Nav->wasPressed()){auto d=Uni.Nav->readDirection();if(d==INavigation::DIR_BACK){rebuild();return;}_view.onNav(d);}return;}ListScreen::onUpdate();}
void NdefRecordFormScreen::onRender(){if(_preview){_view.render(bodyX(),bodyY(),bodyW(),bodyH());return;}ListScreen::onRender();}

const char* NdefLibraryScreen::title(){return _state==BROWSE?"Saved NDEF Records":(_state==DETAILS?"NDEF Details":"NDEF Actions");}
void NdefLibraryScreen::onInit(){browse();}
void NdefLibraryScreen::browse(){_state=BROWSE;if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);Screen.goBack();return;}Uni.Storage->makeDir("/unigeek/nfc");Uni.Storage->makeDir(_dir.c_str());_browser.root=_dir;setItems(_browser.items(),_browser.load(this,_pick,".ndef",nullptr,BrowseFileView::STEM));}
bool NdefLibraryScreen::inspect(const String&p){if(!Uni.Storage||!Uni.Storage->isAvailable())return false;fs::File f=Uni.Storage->open(p.c_str(),"r");if(!f||!f.size()||f.size()>MAXN){if(f)f.close();return false;}uint8_t b[MAXN];_len=f.size();size_t n=f.read(b,_len);f.close();if(n!=_len)return false;NdefParser::Result r;if(!NdefParser::parse(b,n,r)||!r.valid)return false;_text=_url=_phone=_email=_name=_company=_address=_website="";_editable=isSingleRecordMessage(b,n)&&supportedEditable(r);switch(r.kind){case NdefParser::RECORD_TEXT:_recordType="Text";_text=r.text;break;case NdefParser::RECORD_URL:_recordType="URL";_url=r.uri;break;case NdefParser::RECORD_PHONE:_recordType="Phone";_phone=r.phone;break;case NdefParser::RECORD_EMAIL:_recordType="Email";_email=r.email;break;case NdefParser::RECORD_VCARD:_recordType="vCard";_name=r.contact;_company=r.company;_address=r.address;_phone=r.phone;_email=r.email;_website=r.website;break;default:_recordType="Other";_text="";const size_t previewLen=min<size_t>(n,16);for(size_t x=0;x<previewLen;++x){char h[4];snprintf(h,sizeof(h),x?" %02X":"%02X",b[x]);_text+=h;}if(n>previewLen)_text+=" ...";break;}return true;}
void NdefLibraryScreen::details(){_state=DETAILS;_dc=0;auto add=[&](String l,String v){if(_dc>=12)return;_dl[_dc]=l;_dv[_dc]=v.length()?v:"-";_details[_dc]={_dl[_dc].c_str(),_dv[_dc]};++_dc;};add("File",bn(_path));add("Type",_recordType);if(_recordType=="Text")add("Text",_text);else if(_recordType=="URL")add("URL",_url);else if(_recordType=="Phone")add("Phone",_phone);else if(_recordType=="Email")add("Email",_email);else if(_recordType=="vCard"){add("Contact",_name);add("Company",_company);add("Address",_address);add("Phone",_phone);add("Email",_email);add("Website",_website);}else add("Preview",_text);add("Editable",_editable?"Yes":"No");add("Size",String((unsigned)_len)+" bytes");if(_editable)add("[Press]","Actions");_detailsView.resetScroll();_detailsView.setRows(_details,_dc);render();}
void NdefLibraryScreen::onItemSelected(uint8_t i){if(_state==BROWSE){if(i>=_browser.count())return;auto e=_browser.entry(i);if(e.isDir){_pick=e.path;browse();return;}_path=e.path;if(!inspect(_path)){ShowStatusAction::show("Invalid NDEF record",1600);return;}details();return;}if(_state==DETAILS){if(!_editable)return;_state=ACTIONS;_ac=0;_actions[_ac++]={"Make a Copy"};_actions[_ac++]={"Edit"};setItems(_actions,_ac);return;}if(_state==ACTIONS&&i<2&&_editable)Screen.push(new NdefRecordFormScreen(i==0?NdefRecordFormScreen::COPY_FILE:NdefRecordFormScreen::EDIT_FILE,_path));}
void NdefLibraryScreen::onBack(){if(_state==ACTIONS){details();return;}if(_state==DETAILS){browse();return;}Screen.goBack();}
void NdefLibraryScreen::onUpdate(){if(_state!=DETAILS){ListScreen::onUpdate();return;}if(!Uni.Nav->wasPressed())return;auto d=Uni.Nav->readDirection();if(d==INavigation::DIR_BACK){browse();return;}if(d==INavigation::DIR_PRESS){if(!_editable)return;_state=ACTIONS;_ac=0;_actions[_ac++]={"Make a Copy"};_actions[_ac++]={"Edit"};setItems(_actions,_ac);return;}_detailsView.onNav(d);}
void NdefLibraryScreen::onRender(){if(_state==DETAILS){_detailsView.render(bodyX(),bodyY(),bodyW(),bodyH());return;}ListScreen::onRender();}
