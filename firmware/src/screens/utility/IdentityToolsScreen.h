#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "utils/rfid/LFCodec.h"

class UidLibraryScreen : public ListScreen {
public:
  const char* title() override;
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override;
private:
  enum State { BROWSE, DETAILS, ACTIONS } _state = BROWSE;
  BrowseFileView _browser; String _dir = "/unigeek/nfc/uids", _browseDir = "/unigeek/nfc/uids", _path;
  uint8_t _uid[10] = {}; size_t _uidLen = 0;
  ListItem _details[4], _actions[2] = {{"Create Copy"},{"Edit"}};
  String _d0,_d1,_d2;
  void open(); void details();
};

class UidFormScreen : public ListScreen {
public:
  enum Mode { NEW_ITEM, EDIT_ITEM, COPY_ITEM };
  explicit UidFormScreen(Mode m=NEW_ITEM, const String& path="");
  const char* title() override { return "UID"; }
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override;
private:
  enum Source { RANDOM, SAVED, MANUAL } _source = RANDOM;
  enum State { FORM, SOURCE_SELECT, LENGTH_SELECT, FILE_SELECT } _state = FORM;
  Mode _mode; String _path, _pickDir="/unigeek/nfc/uids", _savedPath;
  uint8_t _uid[10]={}; size_t _uidLen=7;
  BrowseFileView _browser; ListItem _items[6]; String _labels[6]; uint8_t _count=0;
  ListItem _sourceItems[3]={{"Random"},{"File"},{"Manual"}};
  ListItem _lengthItems[3]={{"4 bytes"},{"7 bytes"},{"10 bytes"}};
  void rebuild(); void chooseSource(); void chooseLength(); void chooseFile(); void manual(); void randomize(); void save();
};

class IdLibraryScreen : public ListScreen {
public:
  const char* title() override;
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override;
private:
  enum State { BROWSE, DETAILS, ACTIONS } _state=BROWSE;
  BrowseFileView _browser; String _dir="/unigeek/rfid/ids", _browseDir="/unigeek/rfid/ids", _path;
  LFCodec::DecodedData _data; ListItem _details[9], _actions[2]={{"Create Copy"},{"Edit"}};
  String _dl[8],_dv[8]; uint8_t _dc=0; void open(); void details();
};

class IdFormScreen : public ListScreen {
public:
  enum Mode { NEW_ITEM, EDIT_ITEM, COPY_ITEM };
  explicit IdFormScreen(Mode m=NEW_ITEM,const String& path="");
  const char* title() override { return "RFID ID"; }
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override;
private:
  enum Source { RANDOM, SAVED, MANUAL } _source=RANDOM;
  enum State { FORM, TYPE_SELECT, SOURCE_SELECT, FILE_SELECT } _state=FORM;
  Mode _mode; String _path,_savedPath,_pickDir="/unigeek/rfid/ids"; LFCodec::DecodedData _data;
  BrowseFileView _browser; ListItem _items[10],_types[8],_sources[3]={{"Random"},{"File"},{"Manual"}};
  String _labels[10]; uint8_t _count=0,_typeCount=0;
  void rebuild(); void types(); void sources(); void files(); void editField(size_t); void randomize(); void save();
};
