#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class ChameleonMfcToolsScreen : public ListScreen {
public:
  const char* title() override { return "Tag Operations"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State { MENU, UID_FORM, UID_FILE_PICKER, UID_DUMP_PICKER, DUMP_FILE_PICKER } _state = MENU;
  enum UidSource { UID_MANUAL, UID_FILE, UID_DUMP } _uidSource = UID_MANUAL;
  ListItem _items[6];
  ListItem _uidItems[3]; String _uidValues[3]; uint8_t _uidCount=0;
  uint8_t _uid[7]={}; uint8_t _uidLen=0; String _uidFile, _uidDumpFile;
  BrowseFileView _browser;
  String _uidPickDir = "/unigeek/nfc/uids";
  String _dumpPickDir = "/unigeek/nfc/dumps";
  void _writeTag();
  void _writeUid();
  void _rebuildUidForm(uint8_t selected=0);
  void _editUidManual();
  void _startUidWrite();
  void _writeUidFromFile();
  void _writeUidFromDump();
  void _eraseTag();
  void _writeFromFile();
  void _openDumpFileBrowser();
  void _writeFromSlot();
};
