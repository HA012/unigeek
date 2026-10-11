#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/rfid/LFCodec.h"

class LfDataLibraryScreen : public ListScreen {
public:
  const char* title() override;
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override; void onUpdate() override; void onRender() override;
private:
  enum State { BROWSE, DETAILS, ACTIONS } _state=BROWSE;
  BrowseFileView _browser; String _dir="/unigeek/rfid/data", _browseDir="/unigeek/rfid/data", _path;
  LFCodec::DecodedData _data; ScrollListView _detailsView; ScrollListView::Row _details[10]; ListItem _actions[2]={{"Make a Copy"},{"Edit"}};
  String _dl[10],_dv[10]; uint8_t _dc=0;
  void open(); void details(); bool load(const String&);
};

class LfDataFormScreen : public ListScreen {
public:
  enum Mode { NEW_ITEM, EDIT_ITEM, COPY_ITEM };
  explicit LfDataFormScreen(Mode m=NEW_ITEM,const String& path="");
  const char* title() override { return _preview ? "LF Data View" : "LF Data"; }
  void onInit() override; void onItemSelected(uint8_t) override; void onBack() override; void onUpdate() override; void onRender() override;
private:
  enum Source { RANDOM, SAVED, MANUAL } _source=RANDOM;
  enum State { FORM, TYPE_SELECT, SOURCE_SELECT, FILE_SELECT } _state=FORM;
  Mode _mode; String _path,_savedPath,_pickDir="/unigeek/rfid/ids"; LFCodec::DecodedData _data;
  BrowseFileView _browser; ListItem _items[12],_types[8],_sources[3]={{"Random"},{"File"},{"Manual"}};
  bool _preview=false; ScrollListView _view; ScrollListView::Row _rows[10]; String _rl[10],_rv[10]; uint8_t _rc=0;
  String _labels[12]; uint8_t _count=0,_typeCount=0;
  void rebuild(); void types(); void sources(); void files(); void editField(size_t); void randomize();
  void viewData(); void editData(); void save(); bool loadBin(const String&); bool writeBin(const String&);
  String fieldValue(const LFCodec::FieldInfo&) const; String suggestedName() const;
};
