#pragma once
#include "utils/nfc/MfcRecoverySummary.h"

#include <array>
#include <Adafruit_PN532.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/nfc/NFCUtility.h"
#include "utils/nfc/MagicCard.h"

class PN532I2cScreen : public ListScreen
{
public:
  const char* title() override;
  bool inhibitPowerOff() override { return true; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State_e {
    STATE_MAIN_MENU,
    STATE_FAMILIES_MENU,
    STATE_DEVICE_INFO,
    STATE_SCAN_RESULT,
    STATE_SCAN_14A,
    STATE_MIFARE_MENU,
    STATE_MIFARE_TAG_MENU,
    STATE_MIFARE_ADVANCED_MENU,
    STATE_MIFARE_NDEF_MENU,
    STATE_MIFARE_ATTACKS_MENU,
    STATE_MIFARE_KEYS_MENU,
    STATE_MIFARE_KEY_DB_SELECT,
    STATE_MIFARE_KEY_DB_VIEW,
    STATE_MIFARE_DUMP,
    STATE_MIFARE_KEYS,
    STATE_MIFARE_DUMP_SELECT,
    STATE_MIFARE_UID_SOURCE_FORM,
    STATE_MIFARE_UID_SELECT,
    STATE_MIFARE_UID_DUMP_SELECT,
    STATE_MIFARE_WRITE_PREVIEW,
    STATE_MIFARE_UID_WRITE_PREVIEW,
    STATE_DICT_SELECT,
    STATE_ULTRALIGHT_MENU,
    STATE_ULTRALIGHT_TAG_MENU,
    STATE_ULTRALIGHT_ADVANCED_MENU,
    STATE_ULTRALIGHT_NDEF_MENU,
    STATE_TYPEB_MENU,
    STATE_TYPEB_TAG_MENU,
    STATE_TYPEB_ADVANCED_MENU,
    STATE_TYPEB_NDEF_MENU,
    STATE_TYPEB_RESULT,
    STATE_TYPE4A_MENU,
    STATE_TYPE4A_TAG_MENU,
    STATE_TYPE4A_ADVANCED_MENU,
    STATE_TYPE4A_NDEF_MENU,
    STATE_TYPE4A_RESULT,
    STATE_DESFIRE_MENU,
    STATE_DESFIRE_APPS_MENU,
    STATE_DESFIRE_FILES_MENU,
    STATE_DESFIRE_ADVANCED_MENU,
    STATE_DESFIRE_RESULT,
    STATE_FELICA_MENU,
    STATE_FELICA_TAG_MENU,
    STATE_FELICA_SYSTEMS_MENU,
    STATE_FELICA_SERVICES_MENU,
    STATE_FELICA_ADVANCED_MENU,
    STATE_FELICA_NDEF_MENU,
    STATE_FELICA_RESULT,
    STATE_TYPE1_MENU,
    STATE_TYPE1_TAG_MENU,
    STATE_TYPE1_NDEF_MENU,
    STATE_TYPE1_RESULT,
    STATE_MAGIC_DETECT,
    STATE_RAW_RESULT,
    STATE_ULTRALIGHT_DUMP,
    STATE_NDEF_WRITE_MENU,
    STATE_NDEF_RESULT,
    STATE_NDEF_FILE_SELECT,
  };

  State_e      _state    = STATE_MAIN_MENU;
  Adafruit_PN532* _nfc   = nullptr;
  TwoWire*     _wire     = nullptr;
  const char*  _busName  = nullptr;
  bool         _ready    = false;

  // Last scanned 14A card
  uint8_t  _uid[7] = {};
  uint8_t  _uidLen = 0;
  uint16_t _atqa   = 0;
  uint8_t  _sak    = 0;
  bool     _hasCard = false;
  bool     _rawResultMifare = false;
  bool     _rawResultTypeB = false;
  bool     _rawResultTypeBRaw = false;
  bool     _rawResultType4A = false;

  // Last scanned ISO/IEC 14443 Type B card. InListPassiveTarget returns the
  // 12-byte ATQB plus the ATTRIB response after activation.
  uint8_t  _typeBAtqb[12] = {};
  uint8_t  _typeBAttrib[32] = {};
  uint8_t  _typeBAttribLen = 0;
  uint8_t  _typeBTg = 1;
  bool     _hasTypeB = false;
  bool     _typeBFromMainScan = false;
  bool     _typeBRfConfigured = false;
  size_t   _typeBMLe = 48;
  size_t   _typeBMLc = 40;
  uint8_t  _typeBNlenSize = 2;

  // Last activated ISO/IEC 14443-4 Type A target.
  uint8_t  _type4ATg = 1;
  uint8_t  _type4AAts[32] = {};
  uint8_t  _type4AAtsLen = 0;
  bool     _hasType4A = false;

  // DESFire state (uses the activated Type 4A / ISO-DEP target).
  uint8_t  _desfireAid[3] = {};
  bool     _desfireAidSelected = false;
  uint8_t  _desfireAidUid[7] = {};
  uint8_t  _desfireAidUidLen = 0;
  State_e  _desfireResultReturn = STATE_DESFIRE_MENU;
  State_e  _felicaResultReturn = STATE_FELICA_MENU;
  String   _operationResultTitle;

  // Last activated FeliCa target (PN532 native 212 kbps polling).
  uint8_t  _felicaTg = 1;
  uint8_t  _felicaIdm[8] = {};
  uint8_t  _felicaPmm[8] = {};
  uint16_t _felicaSystemCode = 0xFFFF;
  bool     _hasFelica = false;

  // Last activated NFC Forum Type 1 / Jewel target. PN532 BrTy 0x04.
  uint8_t  _type1Tg = 1;
  uint8_t  _type1SensRes[2] = {};
  uint8_t  _type1JewelId[4] = {};
  uint8_t  _type1Hr[2] = {};
  bool     _hasType1 = false;

  // Remember the cursor position of each internal menu. PN532I2cScreen uses
  // one ListScreen instance for several menu states, so Back must restore the
  // parent menu explicitly instead of relying on pointer-based list caching.
  uint8_t _selMain = 0;
  uint8_t _selMifare = 0, _selMifareTag = 0, _selMifareAdvanced = 0;
  uint8_t _selMifareNdef = 0, _selMifareAttacks = 0, _selMifareKeys = 0;
  uint8_t _selUltralight = 0, _selUltralightTag = 0, _selUltralightAdvanced = 0, _selUltralightNdef = 0;
  uint8_t _selTypeB = 0, _selTypeBTag = 0, _selTypeBAdvanced = 0, _selTypeBNdef = 0;
  uint8_t _selType4A = 0, _selType4ATag = 0, _selType4AAdvanced = 0, _selType4ANdef = 0;
  uint8_t _selDesfire = 0, _selDesfireApps = 0, _selDesfireFiles = 0, _selDesfireAdvanced = 0;
  uint8_t _selFelica = 0, _selFelicaTag = 0, _selFelicaSystems = 0, _selFelicaServices = 0, _selFelicaAdvanced = 0, _selFelicaNdef = 0;
  uint8_t _selType1 = 0, _selType1Tag = 0, _selType1Ndef = 0;
  uint8_t _selNdefWrite = 0;
  std::array<std::pair<NFCUtility::MIFARE_Key, NFCUtility::MIFARE_Key>, 40> _mfKeys;

  // Device information reported by GetFirmwareVersion
  uint8_t _fwIc = 0, _fwVer = 0, _fwRev = 0, _fwSup = 0;

  // Card type helpers
  std::pair<size_t, size_t> _mfDims(uint8_t sak) const;

  // Scroll view for info / dump / keys / raw
  ScrollListView _scrollView;
  static constexpr size_t MAX_ROWS = 520;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;

  ListItem _mainItems[4] = {
    {"Scan Tag"}, {"Scan NFC Reader"}, {"Families"}, {"Device Info"},
  };
  ListItem _familyItems[7] = {
    {"MIFARE Classic"}, {"Ultralight / NTAG"},
    {"Type 4A (experimental)"}, {"Type 4B (experimental)"},
    {"DESFire (experimental)"}, {"FeliCa (experimental)"},
    {"Type 1 / Jewel (experimental)"},
  };
  uint8_t _selFamilies = 0;
  bool _familyFromScan = false;
  int8_t _scanFamily = -1;
  void _goFamilies();
  void _openFamily(uint8_t index);
  void _backFromFamily();

  ListItem _mfItems[4] = {
    {"Tag Operations"},
    {"NDEF Operations"},
    {"Attacks"},
    {"Keys"},
  };

  ListItem _mfAttackItems[1] = {
    {"Dictionary"},
  };

  ListItem _mfKeysItems[2] = {
    {"Check Known Keys"},
    {"Dictionaries"},
  };

  ListItem _mfTagItems[6] = {
    {"Detect Magic"},
    {"Read Tag"},
    {"Write UID to Tag"},
    {"Write Dump to Tag"},
    {"Erase Tag"},
    {"Advanced"},
  };

  ListItem _mfAdvancedItems[3] = {
    {"Read Memory"},
    {"Edit Memory"},
    {"Lock UID (Gen3)"},
  };

  ListItem _mfNdefItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Format NDEF"},
    {"Erase NDEF"},
  };

  ListItem _ulItems[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };

  ListItem _ulTagItems[4] = {
    {"Read Tag"},
    {"Write to Tag"},
    {"Erase Tag"},
    {"Advanced"},
  };

  ListItem _ulAdvancedItems[5] = {
    {"Read Memory"},
    {"Edit Memory"},
    {"Set Password"},
    {"Remove Password"},
    {"Lock Tag"},
  };

  ListItem _ulNdefItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Format NDEF"},
    {"Erase NDEF"},
  };

  ListItem _typeBItems[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };

  ListItem _typeBTagItems[2] = {
    {"Read Tag"},
    {"Advanced"},
  };

  ListItem _typeBAdvancedItems[2] = {
    {"Send APDU"},
    {"Raw Commands"},
  };

  ListItem _typeBNdefItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Format NDEF"},
    {"Erase NDEF"},
  };


  ListItem _type4AItems[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };

  ListItem _type4ATagItems[2] = {
    {"Read Tag"},
    {"Advanced"},
  };

  ListItem _type4AAdvancedItems[1] = {
    {"Send APDU"},
  };

  ListItem _type4ANdefItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Format NDEF"},
    {"Erase NDEF"},
  };

  ListItem _desfireItems[4] = {{"Read Tag"}, {"Applications"}, {"Files"}, {"Advanced"}};
  ListItem _desfireAppItems[3] = {{"List Applications"}, {"Select Application"}, {"Application Details"}};
  ListItem _desfireFileItems[4] = {{"List Files"}, {"Read File"}, {"Edit File"}, {"File Details"}};
  ListItem _desfireAdvancedItems[2] = {{"Authenticate"}, {"Send APDU"}};

  ListItem _felicaItems[2] = {{"Tag Operations"}, {"NDEF Operations"}};
  ListItem _felicaTagItems[4] = {{"Read Tag"}, {"Systems"}, {"Services"}, {"Advanced"}};
  ListItem _felicaSystemItems[1] = {{"List Systems"}};
  ListItem _felicaServiceItems[3] = {{"List Services"}, {"Read Service"}, {"Service Details"}};
  ListItem _felicaAdvancedItems[3] = {{"Read Memory"}, {"Edit Memory"}, {"Raw Commands"}};
  ListItem _felicaNdefItems[4] = {{"Read NDEF"}, {"Write NDEF"}, {"Format NDEF"}, {"Erase NDEF"}};

  ListItem _type1Items[2] = {{"Tag Operations"}, {"NDEF Operations"}};
  ListItem _type1TagItems[2] = {{"Read Tag"}, {"Read Memory"}};
  ListItem _type1NdefItems[1] = {{"Read NDEF"}};

  enum MagicUiPhase : uint8_t { MAGIC_WAITING, MAGIC_SCANNING, MAGIC_RESULT };
  MagicUiPhase _magicUiPhase = MAGIC_WAITING;
  String _magicResult;

  ListItem _ndefWriteItems[6] = {
    {"Text"},
    {"URL"},
    {"Phone"},
    {"Email"},
    {"vCard"},
    {"Load from File"},
  };


  // MIFARE dump image — filled by _doDumpMemory(), saved by _doSaveDump()
  static constexpr const char* _nfcPath  = "/unigeek/nfc";
  static constexpr const char* _ndefPath = "/unigeek/nfc/ndefs";
  static constexpr const char* _dumpPath = "/unigeek/nfc/dumps";
  uint8_t  _dumpImg[4096] = {};
  size_t   _dumpLen = 0;
  bool     _hasDump = false;
  bool     _dumpComplete = false;
  size_t   _dumpReadBlocks = 0;
  uint8_t  _dumpValidBlocks[256] = {};
  bool     _resumeReadAfterDict = false;
  bool     _recoverChainActive = false;
  bool     _recoverContinueMissing = false;
  bool     _recoverPromptShown = false;
  bool     _recoverStopRequested = false;
  uint8_t  _recoverChainIndex = 0;
  uint16_t _recoverChainNewKeys = 0;
  MfcRecoverySummary _recoverySummary;
  bool     _pendingRecoveryStep = false;
  bool     _readAfterRecovery = false;
  String   _dumpPickDir;
  String   _uidPickDir;
  uint8_t  _uidWriteSource[7] = {};
  uint8_t  _uidWriteSourceLen = 0;
  enum UidWriteSource_e { UID_WRITE_MANUAL, UID_WRITE_FILE, UID_WRITE_DUMP };
  UidWriteSource_e _uidWriteSourceMode = UID_WRITE_MANUAL;
  String   _uidWriteFilePath;
  String   _uidWriteDumpPath;
  ListItem _uidWriteItems[3];
  bool     _writePreviewFromFile = false;
  bool     _writePreviewSourceUidKnown = false;
  uint8_t  _writePreviewSourceUid[7] = {};
  uint8_t  _writePreviewSourceUidLen = 0;
  bool     _writePreviewReplaceUid = true;
  String   _ulTypeName;
  uint16_t _ulPages = 0;

  enum NdefTarget_e {
    NDEF_TARGET_ULTRALIGHT,
    NDEF_TARGET_MIFARE_CLASSIC,
    NDEF_TARGET_TYPE_B,
    NDEF_TARGET_TYPE_4A,
    NDEF_TARGET_FELICA,
    NDEF_TARGET_TYPE_1,
  };
  NdefTarget_e _ndefTarget = NDEF_TARGET_ULTRALIGHT;

  // Raw NDEF message retained after Read NDEF (without the tag-specific TLV wrapper).
  static constexpr size_t MAX_NDEF_BYTES = 880;
  uint8_t  _ndefBuf[MAX_NDEF_BYTES] = {};
  size_t   _ndefLen = 0;
  size_t   _ndefCapacity = 0;
  bool     _hasNdef = false;
  // Type 4A NDEF transport limits from the Capability Container.
  size_t   _type4AMLe = 48;
  size_t   _type4AMLc = 40;
  uint8_t  _type4ANlenSize = 2;
  bool     _ndefWritePreview = false;
  bool     _ndefWritePreviewFromFile = false;
  String   _ndefPickDir;

  static constexpr const char* _dictPath = "/unigeek/nfc/dictionaries";
  BrowseFileView _browser;
  ListItem       _dictItems[2 + BrowseFileView::kCap];
  String         _dictPickDir;   // current dir in the dict picker
  String         _keyDbPickDir;  // current dir in the key database browser
  String         _keyDbViewTitle;

  bool _initModule();
  void _cleanup();
  void _goMain();
  void _goMifare();
  void _goMifareTag();
  void _goMifareAdvanced();
  void _goMifareNdef();
  void _goMifareAttacks();
  void _goMifareKeys();
  void _openKeyDatabases();
  void _openKeyDatabase(uint8_t index);
  void _goUltralight();
  void _goUltralightTag();
  void _goUltralightAdvanced();
  void _goUltralightNdef();
  void _goTypeB();
  void _goTypeBTag();
  void _goTypeBAdvanced();
  void _goTypeBNdef();
  void _goType4A();
  void _goType4ATag();
  void _goType4AAdvanced();
  void _goType4ANdef();
  void _goDesfire();
  void _goDesfireApps();
  void _goDesfireFiles();
  void _goDesfireAdvanced();
  void _goFelica();
  void _goFelicaTag();
  void _goFelicaSystems();
  void _goFelicaServices();
  void _goFelicaAdvanced();
  void _goFelicaNdef();
  void _goType1();
  void _goType1Tag();
  void _goType1Ndef();
  void _goDetectMagic();

  void _showDeviceInfo();
  void _doScanReader();
  void _doScan14A();
  bool _scanTypeB(uint32_t timeoutMs = 500);
  bool _isoATagPresent(uint32_t timeoutMs = 120);
  void _showTypeBDetails(bool scanAgainHint = false);
  void _doTypeBReadTag();
  void _doTypeBSendApdu(bool rawMode);
  bool _typeBExchange(const uint8_t* tx, size_t txLen, uint8_t* rx, size_t rxCap, size_t& rxLen);
  bool _typeBSelectNdef(size_t& capacity, bool& writable);
  void _doTypeBReadNdef();
  bool _writeTypeBNdefRecord(const uint8_t* ndef, size_t ndefLen);
  void _doTypeBEraseNdef();
  void _doTypeBFormatNdef();
  bool _scanType4A(uint32_t timeoutMs = 500);
  bool _type4AExchange(const uint8_t* tx, size_t txLen, uint8_t* rx, size_t rxCap, size_t& rxLen);
  bool _type4ASelectNdef(size_t& capacity, bool& writable);
  void _showType4ADetails(bool scanAgainHint = false);
  void _doType4AReadTag();
  void _doType4ASendApdu();
  void _doType4AReadNdef();
  bool _writeType4ANdefRecord(const uint8_t* ndef, size_t ndefLen);
  void _doType4AEraseNdef();
  void _doType4AFormatNdef();
  bool _desfireExchange(uint8_t ins, const uint8_t* data, size_t dataLen, uint8_t* out, size_t outMax, size_t& outLen, uint8_t& status);
  bool _desfireProbe();
  bool _desfireSelectCurrentAid();
  bool _desfireAuthenticateAes(uint8_t keyNo, const uint8_t key[16]);
  void _doDesfireReadTag();
  void _doDesfireAppAction(uint8_t index);
  void _doDesfireFileAction(uint8_t index);
  void _doDesfireAdvancedAction(uint8_t index);
  void _showDesfireHex(const char* title, const uint8_t* data, size_t len);
  bool _scanFelica(uint32_t timeoutMs = 500);
  bool _felicaExchange(const uint8_t* body, size_t bodyLen, uint8_t* rx, size_t rxCap, size_t& rxLen);
  bool _felicaRequestSystemCodes(uint16_t* systems, size_t maxSystems, size_t& count);
  bool _felicaSearchService(uint16_t index, uint16_t& serviceCode);
  bool _felicaRequestService(uint16_t serviceCode, uint16_t& keyVersion);
  bool _felicaReadBlock(uint16_t serviceCode, uint16_t block, uint8_t data[16]);
  bool _felicaWriteBlock(uint16_t serviceCode, uint16_t block, const uint8_t data[16]);
  bool _felicaReadNdef(uint8_t* out, size_t outMax, size_t& outLen, size_t& capacity);
  bool _felicaWriteNdef(const uint8_t* ndef, size_t ndefLen, size_t& capacity);
  void _doFelicaReadTag();
  void _doFelicaTagAction(uint8_t index);
  void _doFelicaSystemAction(uint8_t index);
  void _doFelicaServiceAction(uint8_t index);
  void _doFelicaAdvancedAction(uint8_t index);
  void _showFelicaHex(const char* title, const uint8_t* data, size_t len, State_e returnState);
  void _doFelicaReadNdef();
  bool _writeFelicaNdefRecord(const uint8_t* ndef, size_t ndefLen);
  void _doFelicaEraseNdef();
  void _doFelicaFormatNdef();
  bool _scanType1(uint32_t timeoutMs = 500);
  bool _type1Exchange(const uint8_t* tx, size_t txLen, uint8_t* rx, size_t rxCap, size_t& rxLen);
  bool _type1Rid(uint8_t hr[2], uint8_t uid[4]);
  bool _type1ReadByte(uint8_t address, uint8_t& value);
  bool _type1ReadStatic(uint8_t mem[120]);
  void _doType1ReadTag();
  void _doType1ReadMemory();
  void _doType1ReadNdef();
  bool _discoverDefaultKeys(bool checkingProgress = false);
  bool _keyCheckCancelled = false;
  void _loadSavedKeys();
  void _saveKeys();
  bool _hasReadableKeyForEverySector() const;
  void _doReadTag();
  void _doDumpMemory();
  void _showTagDetails();
  void _appendDumpNdefDetails();
  void _appendDumpNdefDetails(const uint8_t* dump, size_t dumpLen, size_t totalSectors);
  void _showDumpActions();
  bool _doWriteDumpToTag(const uint8_t* dump, size_t len,
                         const uint8_t* sourceUid = nullptr, uint8_t sourceUidLen = 0);
  bool _tryWriteMifareBlock(uint16_t block, const uint8_t data[16],
                            const uint8_t key[6], bool useKeyB);
  void _doWriteDumpFromFilePicker();
  void _doWriteUidSource();
  void _rebuildWriteUidForm(uint8_t selected = 0);
  void _editWriteUidManual();
  void _startWriteUidFromForm();
  void _doWriteUidFromFilePicker();
  void _doWriteUidFromDumpPicker();
  void _doWriteUidFileSelected(uint8_t fileIndex);
  void _doWriteUidDumpSelected(uint8_t fileIndex);
  void _showWriteUidPreview(const uint8_t* uid, uint8_t uidLen);
  bool _doWriteUidToTag();
  bool _readGen1aBlock0(uint8_t block0[16]);
  void _doWriteDumpFileSelected(uint8_t fileIndex);
  void _showWriteDumpPreview(const uint8_t* dump, size_t len,
                             const uint8_t* sourceUid = nullptr, uint8_t sourceUidLen = 0,
                             bool fromFile = false);
  void _doEraseTag();
  void _doShowKeys();
  void _doDictionaryPicker();
  void _doDictionaryAttackWithFile(uint8_t fileIndex);
  void _doDictionaryAttackWithPath(const String& filePath);
  void _startRecoverKeys();
  void _doUltralightReadTag();
  void _doUltralightWriteTag();
  void _doUltralightEraseTag();
  void _doMifareReadMemory();
  void _doMifareEditMemory();
  void _doUltralightReadPages();
  void _doUltralightWritePage();
  void _doUltralightLockTag();
  void _doUltralightSetPassword();
  void _doUltralightRemovePassword();
  bool _detectUltralightTag(uint16_t& pages, const char*& typeName);
  bool _readUltralightDump(uint16_t pages, const char* typeName);
  bool _writeUltralightNtag215Dump(const uint8_t* dump, size_t len);
  void _showUltralightTagDetails(const char* typeName, uint16_t pages);
  void _showUltralightDumpActions();
  void _saveUid(const char* typeName);
  void _saveUltralightDump(const char* typeName);
  void _doReadNdef();
  void _doReadClassicNdef();
  void _showNdefResult(const uint8_t* uid, uint8_t uidLen,
                       const uint8_t* ndef, size_t ndefLen);
  void _goNdefWrite();
  void _goNdefParent();
  void _doWriteNdefText();
  void _doWriteNdefUrl();
  void _doWriteNdefEmail();
  void _doWriteNdefPhone();
  void _doWriteNdefVcard();
  void _doWriteNdefFromFile();
  void _doWriteNdefFileSelected(uint8_t fileIndex);
  void _showNdefWritePreview(const uint8_t* ndef, size_t ndefLen, bool fromFile);
  void _showNdefActions();
  void _doSaveNdef();
  void _doWriteCurrentNdef();
  void _doEraseNdef();
  void _doFormatNdef();
  void _doEraseClassicNdef();
  bool _writeNdefRecord(const uint8_t* ndef, size_t ndefLen);
  bool _writeUltralightNdefRecord(const uint8_t* ndef, size_t ndefLen);
  bool _writeClassicNdefRecord(const uint8_t* ndef, size_t ndefLen);
  bool _formatClassic1kNdef();
  bool _classicNdefSectors(uint8_t* sectors, size_t maxSectors, size_t& count);
  bool _classicAuthSector(uint8_t sector, const uint8_t key[6]);
  bool _classicReadNdefArea(const uint8_t* sectors, size_t sectorCount,
                            uint8_t*& area, size_t& areaLen);
  MagicCardType _detectMagicType(void (*progress)(uint8_t) = nullptr);
  bool _writeMagicUid(MagicCardType type, const uint8_t* sourceUid,
                      uint8_t sourceUidLen, const uint8_t block0[16]);
  bool _resetAndReselect();
  void _doDetectMagic();
  void _doGen3LockUid();
  void _doSaveDump();

  String _hexUid(const uint8_t* uid, uint8_t len) const;
  String _hexBlock(const uint8_t* data, uint8_t len) const;
  const char* _inferType(uint8_t sak, uint16_t atqa) const;
  const char* _inferType2Variant();
  bool _scanCardOrShow(uint32_t timeoutMs);
  void _pushRow(const String& label, const String& value);
  void _pushWrappedRow(const String& label, const String& value);
  void _resetRows();
};
