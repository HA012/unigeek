#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/LogView.h"
#include "utils/nfc/MagicCard.h"

class ST25R3916Backend;

class ST25R3916Screen : public ListScreen {
public:
  const char* title() override {
    switch (_state) {
      case STATE_SCANNING: return "Scan Tag";
      case STATE_DETAILS:
      case STATE_MFC_DETAILS: return "Tag Details";
      case STATE_MFC_MENU: return "MIFARE Classic";
      case STATE_MFU_MENU: return "Ultralight / NTAG";
      case STATE_MFU_TAG_MENU: return "Tag Operations";
      case STATE_MFU_ADVANCED_MENU: return "Advanced";
      case STATE_MFU_MEMORY: return _advancedOperationTitle.length() ? _advancedOperationTitle.c_str() : "Read Memory";
      case STATE_MFU_READING: return "Read Tag";
      case STATE_MFU_DETAILS: return "Tag Details";
      case STATE_MFU_DUMP_HEX: return "Memory Dump";
      case STATE_MFU_DUMP_SELECT: return "Write to Tag";
      case STATE_MFU_WRITE_PREVIEW: return "Write to Tag";
      case STATE_MFU_WRITING: return "Write to Tag";
      case STATE_MFU_ERASING: return "Erase Tag";
      case STATE_MFU_NDEF_MENU: return "NDEF Operations";
      case STATE_MFU_NDEF_WRITE_MENU: return "Write NDEF";
      case STATE_MFU_NDEF_FILE_SELECT: return "NDEF Files";
      case STATE_MFU_NDEF_READING: return "Read NDEF";
      case STATE_MFU_NDEF_WRITING: return "Write NDEF";
      case STATE_MFU_NDEF_DETAILS: return "NDEF Details";
      case STATE_EMULATING: return "Emulate Tag";
      case STATE_MFC_TAG_MENU: return "Tag Operations";
      case STATE_MFC_ADVANCED_MENU: return "Advanced";
      case STATE_MFC_MEMORY: return "Read Memory";
      case STATE_MFC_NDEF_MENU: return "NDEF Operations";
      case STATE_MFC_ATTACKS_MENU: return "Attacks";
      case STATE_MFC_KEYS_MENU: return "Keys";
      case STATE_MFC_KEYS_VIEW: return "Check Known Keys";
      case STATE_MFC_DICT_SELECT: return "Dictionaries";
      case STATE_MFC_DICT_ATTACK_SELECT: return "Dictionary Attack";
      case STATE_MFC_DICT_VIEW: return _dictViewTitle.length() ? _dictViewTitle.c_str() : "Dictionary";
      case STATE_MAGIC_DETECT: return "Detect Magic";
      case STATE_DEVICE_INFO: return "Device Info";
      case STATE_EXP_MENU: return _expFamilyTitle();
      case STATE_EXP_TAG_MENU: return "Tag Operations";
      case STATE_EXP_ADVANCED_MENU: return "Advanced";
      case STATE_EXP_SUB1_MENU: return _expSub1Title();
      case STATE_EXP_SUB2_MENU: return _expSub2Title();
      case STATE_EXP_DETAILS: return "Tag Details";
      case STATE_EXP_RESULT: return _advancedOperationTitle.length() ? _advancedOperationTitle.c_str() : "Result";
      case STATE_EXP_NDEF_MENU: return "NDEF Operations";
      case STATE_EXP_NDEF_WRITE_MENU: return "Write NDEF";
      case STATE_EXP_NDEF_FILE_SELECT: return "NDEF Files";
      case STATE_EXP_NDEF_DETAILS: return "NDEF Details";
      case STATE_EXP_WORKING: return _advancedOperationTitle.length() ? _advancedOperationTitle.c_str() : "Experimental";
      case STATE_MFC_NDEF_WRITE_MENU: return "Write NDEF";
      case STATE_MFC_NDEF_FILE_SELECT: return "NDEF Files";
      case STATE_MFC_NDEF_READING: return "Read NDEF";
      case STATE_MFC_NDEF_WRITING: return "Write NDEF";
      case STATE_MFC_NDEF_DETAILS: return "NDEF Details";
      case STATE_MFC_READING: return "Read Tag";
      case STATE_MFC_DUMP_HEX: return "Memory Dump";
      case STATE_MFC_DUMP_SELECT: return "Write to Tag";
      case STATE_MFC_WRITE_PREVIEW: return "Write to Tag";
      case STATE_MFC_WRITING: return "Write to Tag";
      case STATE_MFC_ERASING: return "Erase Tag";
      default: return "ST25R3916";
    }
  }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t {
    STATE_MENU,
    STATE_SCANNING,
    STATE_DETAILS,
    STATE_MFC_MENU,
    STATE_MFC_TAG_MENU,
    STATE_MFC_ADVANCED_MENU,
    STATE_MFC_MEMORY,
    STATE_MFC_NDEF_MENU,
    STATE_MFC_ATTACKS_MENU,
    STATE_MFC_KEYS_MENU,
    STATE_MFC_KEYS_VIEW,
    STATE_MFC_DICT_SELECT,
    STATE_MFC_DICT_VIEW,
    STATE_MFC_DICT_ATTACK_SELECT,
    STATE_MAGIC_DETECT,
    STATE_DEVICE_INFO,
    STATE_MFC_NDEF_WRITE_MENU,
    STATE_MFC_NDEF_FILE_SELECT,
    STATE_MFC_NDEF_READING,
    STATE_MFC_NDEF_WRITING,
    STATE_MFC_NDEF_DETAILS,
    STATE_MFC_READING,
    STATE_MFC_DETAILS,
    STATE_MFC_DUMP_HEX,
    STATE_MFC_DUMP_SELECT,
    STATE_MFC_WRITE_PREVIEW,
    STATE_MFC_WRITING,
    STATE_MFC_ERASING,
    STATE_MFU_MENU,
    STATE_MFU_TAG_MENU,
    STATE_MFU_ADVANCED_MENU,
    STATE_MFU_MEMORY,
    STATE_MFU_READING,
    STATE_MFU_DETAILS,
    STATE_MFU_DUMP_HEX,
    STATE_MFU_DUMP_SELECT,
    STATE_MFU_WRITE_PREVIEW,
    STATE_MFU_WRITING,
    STATE_MFU_ERASING,
    STATE_MFU_NDEF_MENU,
    STATE_MFU_NDEF_WRITE_MENU,
    STATE_MFU_NDEF_FILE_SELECT,
    STATE_MFU_NDEF_READING,
    STATE_MFU_NDEF_WRITING,
    STATE_MFU_NDEF_DETAILS,
    STATE_EXP_MENU,
    STATE_EXP_TAG_MENU,
    STATE_EXP_ADVANCED_MENU,
    STATE_EXP_SUB1_MENU,
    STATE_EXP_SUB2_MENU,
    STATE_EXP_DETAILS,
    STATE_EXP_RESULT,
    STATE_EXP_NDEF_MENU,
    STATE_EXP_NDEF_WRITE_MENU,
    STATE_EXP_NDEF_FILE_SELECT,
    STATE_EXP_NDEF_DETAILS,
    STATE_EXP_WORKING,
    STATE_EMULATING,
  };

  State _state = STATE_MENU;
  uint16_t _lastTechMask = 0;
  // Preserve the cursor when returning from child screens/operations.
  uint8_t _selMain = 0, _selMfc = 0, _selMfcTag = 0, _selMfcAdvanced = 0, _selMfcNdef = 0;
  uint8_t _selMfcAttacks = 0, _selMfcKeys = 0;
  uint8_t _selMfu = 0, _selMfuTag = 0, _selMfuNdef = 0, _selMfuAdvanced = 0;
  uint8_t _selExp = 0, _selExpTag = 0, _selExpAdvanced = 0, _selExpSub1 = 0, _selExpSub2 = 0, _selExpNdef = 0;

  enum ExperimentalFamily : uint8_t { EXP_NONE, EXP_DESFIRE, EXP_NFCV, EXP_FELICA, EXP_TYPE4B };
  ExperimentalFamily _expFamily = EXP_NONE;
  State _expResultReturn = STATE_EXP_TAG_MENU;
  uint8_t _expDesfireAid[3] = {};
  bool _expDesfireAidSelected = false;

  ListItem _items[8] = {
    {"Scan Tag"},
    {"MIFARE Classic"},
    {"Ultralight / NTAG"},
    {"DESFire (experimental)"},
    {"ICODE / ST25V (experimental)"},
    {"FeliCa (experimental)"},
    {"Type 4B (experimental)"},
    {"Device Info"},
  };
  ListItem _mfcItems[4] = {
    {"Tag Operations"},
    {"NDEF Operations"},
    {"Attacks"},
    {"Keys"},
  };
  ListItem _mfcAttackItems[3] = {
    {"Dictionary Attack"},
    {"Static Nested"},
    {"Nested Attack"},
  };
  ListItem _mfcKeysItems[2] = {
    {"Check Known Keys"},
    {"Dictionaries"},
  };
  ListItem _mfcTagItems[6] = {
    {"Detect Magic"},
    {"Read Tag"},
    {"Write to Tag"},
    {"Erase Tag"},
    {"Emulate UID"},
    {"Advanced"},
  };
  ListItem _mfcAdvancedItems[4] = {
    {"Read Memory"},
    {"Edit Memory"},
    {"Edit UID (Gen1A/Gen3)"},
    {"Lock UID (Gen3)"},
  };
  ListItem _mfcNdefItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Format NDEF"},
    {"Erase NDEF"},
  };
  ListItem _mfuItems[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };
  ListItem _mfuNdefItems[4] = {
    {"Read NDEF"}, {"Write NDEF"}, {"Format NDEF"}, {"Erase NDEF"},
  };
  ListItem _mfuTagItems[5] = {
    {"Read Tag"},
    {"Write to Tag"},
    {"Emulate Tag"},
    {"Erase Tag"},
    {"Advanced"},
  };
  ListItem _mfuAdvancedItems[5] = {
    {"Read Memory"},
    {"Edit Memory"},
    {"Set Password"},
    {"Remove Password"},
    {"Lock Tag"},
  };
  ListItem _expRootItems[2] = {{"Tag Operations"}, {"NDEF Operations"}};
  ListItem _expNdefItems[4] = {{"Read NDEF"}, {"Write NDEF"}, {"Format NDEF"}, {"Erase NDEF"}};
  ListItem _expDesfireTagItems[4] = {{"Read Tag"}, {"Applications"}, {"Files"}, {"Advanced"}};
  ListItem _expDesfireAppItems[3] = {{"List Applications"}, {"Select Application"}, {"Application Details"}};
  ListItem _expDesfireFileItems[4] = {{"List Files"}, {"Read File"}, {"Edit File"}, {"File Details"}};
  ListItem _expDesfireAdvancedItems[2] = {{"Authenticate"}, {"Send APDU"}};
  ListItem _expNfcvTagItems[4] = {{"Read Tag"}, {"Write to Tag"}, {"Erase Tag"}, {"Advanced"}};
  ListItem _expNfcvAdvancedItems[5] = {{"Read Memory"}, {"Edit Memory"}, {"Security Status"}, {"Password"}, {"Lock Block"}};
  ListItem _expFelicaTagItems[4] = {{"Read Tag"}, {"Systems"}, {"Services"}, {"Advanced"}};
  ListItem _expFelicaSystemItems[1] = {{"List Systems"}};
  ListItem _expFelicaServiceItems[3] = {{"List Services"}, {"Read Service"}, {"Service Details"}};
  ListItem _expFelicaAdvancedItems[3] = {{"Read Memory"}, {"Edit Memory"}, {"Raw Commands"}};
  ListItem _expType4bTagItems[2] = {{"Read Tag"}, {"Advanced"}};
  ListItem _expType4bAdvancedItems[2] = {{"Send APDU"}, {"Raw Commands"}};
  ListItem _mfcNdefWriteItems[6] = {
    {"Text"}, {"URL"}, {"Phone"}, {"Email"}, {"vCard"}, {"Load from File"},
  };

  static constexpr uint8_t kMaxRows = 96;
  ScrollListView _scrollView;
  LogView _magicLog;
  bool _magicDetectDone = false;
  ScrollListView::Row _rows[kMaxRows];
  String _rowLabels[kMaxRows];
  String _rowValues[kMaxRows];
  uint8_t _rowCount = 0;

  static constexpr size_t kMfcMaxDumpLen = 4096;
  static constexpr size_t kMfuMaxDumpLen = 924;
  uint8_t _mfcDump[kMfcMaxDumpLen] = {};
  size_t _mfcDumpLen = 0;
  uint16_t _mfcDumpBlocks = 0;
  uint16_t _mfcDumpOffset = 0;
  uint8_t _mfcUid[10] = {};
  uint8_t _mfcUidLen = 0;
  uint8_t _mfcSak = 0;
  uint8_t _mfcAtqa[2] = {};
  uint8_t _mfuDump[kMfuMaxDumpLen] = {};
  size_t _mfuDumpLen = 0;
  uint16_t _mfuPages = 0;
  uint8_t _mfuUid[10] = {};
  uint8_t _mfuUidLen = 0;
  uint8_t _mfuAtqa[2] = {};
  uint8_t _mfuSak = 0;
  uint16_t _mfuDumpOffset = 0;
  String _mfuType;
  String _advancedOperationTitle;
  bool _writePreviewFromFile = false;
  bool _mfuWritePreviewFromFile = false;
  bool _mfcDumpFromCompleteRead = false;
  uint8_t _writeSourceUid[4] = {};
  static constexpr size_t kMaxNdefBytes = 254;
  uint8_t _ndefBuf[kMaxNdefBytes] = {};
  size_t _ndefLen = 0;
  size_t _ndefCapacity = 0;
  bool _hasNdef = false;
  bool _ndefWritePreview = false;
  bool _ndefWritePreviewFromFile = false;
  bool _ndefMfuTarget = false;
  bool _ndefExperimentalTarget = false;
  bool _writeSourceUidKnown = false;
  BrowseFileView _browser;
  String _dumpPickDir;
  String _ndefPickDir;
  String _dictPickDir;
  String _dictViewTitle;
  ST25R3916Backend* _emuDev = nullptr;
  bool _emuReturnMfc = false;
  static constexpr const char* _dictPath = "/unigeek/nfc/dictionaries";

  void _scan(uint16_t techMask);
  void _readMfcTag();
  void _emulateMfcTag();
  void _emulateMfuTag();
  void _stopEmulation();
  void _showMfcDumpActions();
  void _openMfcDumpPicker();
  void _openMfcDumpFile(uint8_t index);
  void _showMfcWritePreview(const uint8_t* dump, size_t len, bool fromFile);
  bool _writeMfcDumpToTag();
  void _eraseMfcTag();
  void _saveMfcDump();
  void _renderMfcDump();
  void _handleMfcDumpNav(INavigation::Direction dir);
  void _showDeviceInfo();
  void _showStatusAndReturn(const char* message, State target, int32_t durationMs = 1600);
  void _showMenu();
  void _showMfcMenu();
  void _showMfcTagMenu();
  void _showMfcAdvancedMenu();
  void _readMfcMemory();
  void _editMfcMemory();
  void _editMfcUid();
  void _lockMfcUidGen3();
  void _showMfcNdefMenu();
  void _showMfcNdefWriteMenu();
  void _showMfcAttacksMenu();
  void _showMfcKeysMenu();
  void _showMfcKnownKeys();
  void _openMfcDictionaries(bool attackMode = false);
  void _openMfcDictionary(uint8_t index, bool attackMode = false);
  void _runMfcDictionaryAttack(const String& path);
  void _detectMagic();
  void _runDetectMagic();
  MagicCardType _detectMagicType(class ST25R3916Backend& dev);
  bool _writeMagicUid(class ST25R3916Backend& dev, MagicCardType type, const uint8_t* uid, uint8_t uidLen, const uint8_t block0[16]);
  void _showMfuMenu();
  void _showMfuTagMenu();
  void _showMfuAdvancedMenu();
  void _readMfuMemory();
  void _editMfuMemory();
  void _setMfuPassword();
  void _removeMfuPassword();
  void _lockMfuTag();
  void _formatMfuNdef();
  void _showMfuNdefMenu();
  void _showMfuNdefWriteMenu();
  void _readMfuNdef();
  bool _writeMfuNdef(const uint8_t* ndef, size_t ndefLen);
  void _eraseMfuNdef();
  void _readMfuTag();
  void _showMfuDumpActions();
  void _openMfuDumpPicker();
  void _openMfuDumpFile(uint8_t index);
  void _showMfuWritePreview(bool fromFile);
  bool _writeMfuDumpToTag();
  void _eraseMfuTag();
  void _saveMfuDump();
  void _renderMfuDump();
  void _handleMfuDumpNav(INavigation::Direction dir);
  bool _detectMfuType(class ST25R3916Backend& dev, String& type, uint16_t& pages);
  void _readMfcNdef();
  bool _writeMfcNdef(const uint8_t* ndef, size_t ndefLen);
  void _eraseMfcNdef();
  bool _formatMfc1kNdef();
  void _showNdefWritePreview(const uint8_t* ndef, size_t ndefLen, bool fromFile);
  void _writeNdefBuilt(uint8_t kind);
  void _writeNdefVcard();
  void _openNdefFilePicker();
  void _openNdefFile(uint8_t index);
  void _showNdefDetails(const uint8_t* uid, uint8_t uidLen, const uint8_t* ndef, size_t ndefLen);
  void _addWrappedRow(const String& label, const String& value);
  const char* _expFamilyTitle() const;
  const char* _expSub1Title() const;
  const char* _expSub2Title() const;
  void _showExperimentalMenu(ExperimentalFamily family);
  void _showExperimentalTagMenu();
  void _showExperimentalAdvancedMenu();
  void _showExperimentalSub1Menu();
  void _showExperimentalSub2Menu();
  void _showExperimentalNdefMenu();
  void _showExperimentalNdefWriteMenu();
  void _experimentalReadTag();
  void _experimentalDesfireAction(uint8_t group, uint8_t index);
  void _experimentalNfcvAction(uint8_t index);
  void _experimentalFelicaAction(uint8_t group, uint8_t index);
  void _experimentalType4bAction(uint8_t index);
  void _experimentalReadNdef();
  bool _experimentalWriteNdef(const uint8_t* ndef, size_t ndefLen, bool formatOnly = false);
  void _experimentalEraseNdef(bool formatOnly);
  void _experimentalSendApdu(bool desfire);
  void _experimentalRawCommand();
  void _showExperimentalHex(const char* title, const uint8_t* data, size_t len);
  void _returnToNdefWriteMenu();
  void _renderTagPrompt();
};
