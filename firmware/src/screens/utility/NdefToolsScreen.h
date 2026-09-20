#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"

class NdefRecordFormScreen : public ListScreen {
public:
  using SaveCallback = bool (*)(void*, const uint8_t*, size_t);
  enum Mode { NEW_FILE, EDIT_FILE, COPY_FILE, BUFFER };
  explicit NdefRecordFormScreen(Mode mode=NEW_FILE, const String& path="", SaveCallback cb=nullptr, void* ctx=nullptr);
  const char* title() override { return _preview ? "NDEF Preview" : "NDEF Record"; }
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override; void onUpdate() override; void onRender() override;
  static bool isValidSingleRecord(const uint8_t*, size_t);
private:
  enum Type { TEXT, URL, PHONE, EMAIL, VCARD } _type=TEXT;
  enum State { FORM, TYPE_SELECT } _state=FORM;
  Mode _mode; String _path; SaveCallback _cb; void* _ctx;
  String _text,_url,_phone,_email,_name,_company,_address,_vphone,_vemail,_website;
  ListItem _items[10]; String _vals[10]; uint8_t _count=0; ListItem _types[5]={{"Text"},{"URL"},{"Phone"},{"Email"},{"vCard"}};
  bool _preview=false; ScrollListView _view; ScrollListView::Row _rows[32]; String _rl[32],_rv[32]; uint8_t _rc=0;
  void rebuild(); void selectType(); void editField(uint8_t); bool build(uint8_t*,size_t&); bool load(const String&); bool parse(const uint8_t*,size_t); bool complete() const; void preview(); void save(); String suggested() const;
};

class NdefLibraryScreen : public ListScreen {
public:
  const char* title() override;
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override;
private:
  enum State { BROWSE, DETAILS, ACTIONS } _state=BROWSE; BrowseFileView _browser; String _dir="/unigeek/nfc/ndefs",_pick="/unigeek/nfc/ndefs",_path; bool _editable=false; size_t _len=0;
  ListItem _details[5]; String _dl[5],_dv[5]; uint8_t _dc=0; ListItem _actions[2]; uint8_t _ac=0;
  void browse(); void details(); bool inspect(const String&); void copyRaw();
};
