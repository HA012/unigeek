#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include "ui/views/BrowseFileView.h"

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
      case STATE_MFC_TAG_MENU: return "Tag Operations";
      case STATE_MFC_NDEF_MENU: return "NDEF Operations";
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
    STATE_MFC_NDEF_MENU,
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
  };

  State _state = STATE_MENU;
  uint16_t _lastTechMask = 0;

  ListItem _items[5] = {
    {"Scan Tag", "Auto I2C / SPI"},
    {"MIFARE Classic"},
    {"Ultralight / NTAG"},
    {"Device Info (I2C)", "Grove / U216"},
    {"Device Info (SPI)", "Cap / shared SPI"},
  };
  ListItem _mfcItems[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };
  ListItem _mfcTagItems[3] = {
    {"Read Tag"},
    {"Write to Tag"},
    {"Erase Tag"},
  };
  ListItem _mfcNdefItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Erase NDEF"},
    {"Format NDEF"},
  };
  ListItem _mfuItems[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };
  ListItem _mfuNdefItems[3] = {
    {"Read NDEF"}, {"Write NDEF"}, {"Erase NDEF"},
  };
  ListItem _mfuTagItems[3] = {
    {"Read Tag"},
    {"Write to Tag"},
    {"Erase Tag"},
  };
  ListItem _mfcNdefWriteItems[6] = {
    {"Text"}, {"URL"}, {"Phone"}, {"Email"}, {"vCard"}, {"Load from File"},
  };

  static constexpr uint8_t kMaxRows = 48;
  ScrollListView _scrollView;
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
  uint8_t _mfuDump[kMfuMaxDumpLen] = {};
  size_t _mfuDumpLen = 0;
  uint16_t _mfuPages = 0;
  uint8_t _mfuUid[10] = {};
  uint8_t _mfuUidLen = 0;
  uint8_t _mfuAtqa[2] = {};
  uint8_t _mfuSak = 0;
  uint16_t _mfuDumpOffset = 0;
  String _mfuType;
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
  bool _writeSourceUidKnown = false;
  BrowseFileView _browser;
  String _dumpPickDir;
  String _ndefPickDir;

  void _scan(uint16_t techMask);
  void _readMfcTag();
  void _showMfcDumpActions();
  void _openMfcDumpPicker();
  void _openMfcDumpFile(uint8_t index);
  void _showMfcWritePreview(const uint8_t* dump, size_t len, bool fromFile);
  bool _writeMfcDumpToTag();
  void _eraseMfcTag();
  void _saveMfcDump();
  void _renderMfcDump();
  void _handleMfcDumpNav(INavigation::Direction dir);
  void _showI2CInfo();
  void _showSPIInfo();
  void _showMenu();
  void _showMfcMenu();
  void _showMfcTagMenu();
  void _showMfcNdefMenu();
  void _showMfcNdefWriteMenu();
  void _showMfuMenu();
  void _showMfuTagMenu();
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
  void _renderTagPrompt();
};
