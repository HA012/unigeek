#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/nfc/HfDumpParser.h"

class HfDumpLibraryScreen : public ListScreen {
public:
  const char* title() override;
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  enum State { BROWSE, DETAILS, ACTIONS } _state = BROWSE;
  BrowseFileView _browser;
  String _dir = "/unigeek/nfc/dumps";
  String _browseDir = "/unigeek/nfc/dumps";
  String _path;
  HfDumpParser::Info _info;
  ListItem _details[7]; String _dl[7], _dv[7]; uint8_t _dc = 0;
  ListItem _actions[2] = {{"Create Copy"},{"Edit"}};
  void open(); void details(); bool inspect(const String& path);
};

class HfDumpFormScreen : public ListScreen {
public:
  enum Mode { NEW_ITEM, EDIT_ITEM, COPY_ITEM };
  explicit HfDumpFormScreen(Mode mode = NEW_ITEM, const String& path = "");
  ~HfDumpFormScreen() override;
  const char* title() override { return _passwordView ? "Password Info" : "HF Dump"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
  void onUpdate() override;
  void onRender() override;
private:
  enum State { FORM, TYPE_SELECT, UID_SOURCE_SELECT, UID_FILE_SELECT, NDEF_SOURCE_SELECT, NDEF_FILE_SELECT } _state = FORM;
  enum UidSource { UID_RANDOM, UID_CURRENT, UID_SAVED, UID_MANUAL } _uidSource = UID_RANDOM;
  enum NdefSource { NDEF_NONE, NDEF_CURRENT, NDEF_SAVED, NDEF_NEW } _ndefSource = NDEF_NONE;
  Mode _mode; String _path;
  uint8_t* _dump = nullptr; size_t _dumpLen = 0; HfDumpParser::Info _info;
  uint8_t _uid[10] = {}; uint8_t _uidLen = 0; String _uidFile;
  uint8_t _ndef[1024] = {}; size_t _ndefLen = 0; String _ndefFile;
  BrowseFileView _browser; String _uidPickDir; String _ndefPickDir;
  static constexpr uint8_t MAX_ROWS = 12;
  ListItem _items[MAX_ROWS]; String _values[MAX_ROWS]; uint8_t _count = 0;
  ListItem _types[7] = {{"MIFARE Classic 1K"},{"MIFARE Classic 4K"},{"NTAG210"},{"NTAG212"},{"NTAG213"},{"NTAG215"},{"NTAG216"}};
  ListItem _uidSourcesNew[3] = {{"Random"},{"File"},{"Manual"}};
  ListItem _uidSourcesExisting[4] = {{"Current"},{"Random"},{"File"},{"Manual"}};
  ListItem _ndefSourcesNew[3] = {{"None"},{"File"},{"New"}};
  ListItem _ndefSourcesExisting[4] = {{"Current"},{"None"},{"File"},{"New"}};
  bool _passwordView=false; ScrollListView _passwordList; ScrollListView::Row _passwordRows[7]; String _passwordLabels[7], _passwordValues[7]; uint8_t _passwordCount=0;
  void rebuild(); bool load(const String& path); void freeDump();
  bool buildDefault(uint8_t typeIndex); bool rebuildForType(uint8_t typeIndex);
  void randomUid(); bool applyUid(); bool applyNdef();
  void selectType(); void selectUidSource(); void browseUid(); void selectNdefSource(); void browseNdef();
  static bool receiveNdef(void* context, const uint8_t* ndef, size_t len);
  void editUid(); void viewMemory(); void editMemory(); void showPasswordInfo(); void setPassword(); void removePassword(); void save(); bool writeFile(const String& path);
  String uidText() const; String suggestedName() const;
};
