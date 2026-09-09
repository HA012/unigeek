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
      case STATE_MFC_TAG_MENU: return "Tag Operations";
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
    STATE_MFC_READING,
    STATE_MFC_DETAILS,
    STATE_MFC_DUMP_HEX,
    STATE_MFC_DUMP_SELECT,
    STATE_MFC_WRITE_PREVIEW,
    STATE_MFC_WRITING,
    STATE_MFC_ERASING,
  };

  State _state = STATE_MENU;
  uint16_t _lastTechMask = 0;

  ListItem _items[8] = {
    {"Scan Tag", "Auto I2C / SPI"},
    {"MIFARE Classic"},
    {"NFC-A", "ISO14443A"},
    {"NFC-B", "ISO14443B"},
    {"NFC-F / FeliCa", "212 kbps"},
    {"NFC-V / ISO15693"},
    {"Device Info (I2C)", "Grove / U216"},
    {"Device Info (SPI)", "Cap / shared SPI"},
  };
  ListItem _mfcItems[1] = {
    {"Tag Operations"},
  };
  ListItem _mfcTagItems[3] = {
    {"Read Tag"},
    {"Write to Tag"},
    {"Erase Tag"},
  };

  static constexpr uint8_t kMaxRows = 12;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _rowLabels[kMaxRows];
  String _rowValues[kMaxRows];
  uint8_t _rowCount = 0;

  static constexpr size_t kMfcMaxDumpLen = 4096;
  uint8_t _mfcDump[kMfcMaxDumpLen] = {};
  size_t _mfcDumpLen = 0;
  uint16_t _mfcDumpBlocks = 0;
  uint16_t _mfcDumpOffset = 0;
  uint8_t _mfcUid[10] = {};
  uint8_t _mfcUidLen = 0;
  uint8_t _mfcSak = 0;
  bool _writePreviewFromFile = false;
  bool _mfcDumpFromCompleteRead = false;
  uint8_t _writeSourceUid[4] = {};
  bool _writeSourceUidKnown = false;
  BrowseFileView _browser;
  String _dumpPickDir;

  void _scan(uint16_t techMask);
  void _readMfcTag();
  void _showMfcDumpActions();
  void _showMfcWriteSources();
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
  void _renderTagPrompt();
};
