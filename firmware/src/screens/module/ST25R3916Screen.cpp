#include "ST25R3916Screen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "core/ConfigManager.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/NdefParser.h"
#include "utils/nfc/NdefBuilder.h"
#include "utils/nfc/NfcDumpBuilder.h"

#if defined(DEVICE_HAS_ST25R3916)
#include "utils/nfc/NFCUtility.h"
#include "utils/nfc/ST25R3916Backend.h"
#endif

namespace {
const char* inferNfcAType(uint8_t sak, const uint8_t atqa[2]) {
  if (sak == 0x09) return "MIFARE Classic Mini";
  if (sak == 0x08) return "MIFARE Classic 1K";
  if (sak == 0x18) return "MIFARE Classic 4K";
  if (sak == 0x28) return "MIFARE Plus / SmartMX";
  if (sak == 0x20) {
    if (atqa[0] == 0x03) return "MIFARE DESFire";
    return "ISO14443-4";
  }
  if (sak == 0x00) {
    if (atqa[1] == 0x44) return "MIFARE Ultralight / NTAG";
    return "ISO14443A T2";
  }
  return "ISO14443A";
}

bool isMifareClassic(uint8_t sak) {
  return sak == 0x09 || sak == 0x08 || sak == 0x18;
}

void mfcDimensions(uint8_t sak, size_t& sectors, size_t& blocks) {
  sectors = 0;
  blocks = 0;
  if (sak == 0x09) { sectors = 5; blocks = 20; }
  else if (sak == 0x08) { sectors = 16; blocks = 64; }
  else if (sak == 0x18) { sectors = 40; blocks = 256; }
}

size_t sectorFirstBlock(size_t sector) {
  return sector < 32 ? sector * 4U : 128U + (sector - 32U) * 16U;
}

size_t sectorBlockCount(size_t sector) {
  return sector < 32 ? 4U : 16U;
}

#if defined(DEVICE_HAS_ST25R3916)
bool sameTag(const ST25R3916Backend::ScanResult& a,
             const ST25R3916Backend::ScanResult& b) {
  return a.nfcidLen == b.nfcidLen && a.nfcidLen > 0 &&
         memcmp(a.nfcid, b.nfcid, a.nfcidLen) == 0;
}
#endif
}

void ST25R3916Screen::onInit() {
  _showMenu();
}

void ST25R3916Screen::onUpdate() {
  if (_state == STATE_MENU || _state == STATE_MFC_MENU || _state == STATE_MFC_TAG_MENU ||
      _state == STATE_MFU_MENU || _state == STATE_MFU_TAG_MENU || _state == STATE_MFU_DUMP_SELECT ||
      _state == STATE_MFC_NDEF_MENU || _state == STATE_MFC_NDEF_WRITE_MENU ||
      _state == STATE_MFC_NDEF_FILE_SELECT || _state == STATE_MFC_DUMP_SELECT) {
    ListScreen::onUpdate();
    return;
  }

  if (!Uni.Nav->wasPressed()) return;

  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    if (_state == STATE_MFC_DUMP_HEX) {
      _state = STATE_MFC_DETAILS;
      render();
    } else if (_state == STATE_MFC_NDEF_DETAILS) {
      if (_ndefWritePreview) {
        const bool fromFile = _ndefWritePreviewFromFile;
        _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
        if (fromFile) _openNdefFilePicker(); else _showMfcNdefWriteMenu();
      } else _showMfcNdefMenu();
    } else if (_state == STATE_MFU_DUMP_HEX) {
      _state = STATE_MFU_DETAILS;
      render();
    } else if (_state == STATE_MFU_DETAILS) {
      _showMfuTagMenu();
    } else if (_state == STATE_MFC_DETAILS || _state == STATE_MFC_WRITE_PREVIEW ||
               _state == STATE_MFC_WRITING) {
      _showMfcTagMenu();
    } else {
      _showMenu();
    }
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_DETAILS) {
    _scan(_lastTechMask);
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFU_DETAILS) {
    _showMfuDumpActions();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFU_WRITE_PREVIEW) {
    _writeMfuDumpToTag();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFC_WRITE_PREVIEW) {
    _writeMfcDumpToTag();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFC_DETAILS) {
    _showMfcDumpActions();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFC_NDEF_DETAILS && _ndefWritePreview) {
    const bool fromFile = _ndefWritePreviewFromFile;
    if (_writeMfcNdef(_ndefBuf, _ndefLen)) _showMfcNdefMenu();
    else _showNdefWritePreview(_ndefBuf, _ndefLen, fromFile);
    return;
  }
  if (_state == STATE_MFC_DUMP_HEX) {
    _handleMfcDumpNav(dir);
    return;
  }
  if (_state == STATE_MFU_DUMP_HEX) {
    _handleMfuDumpNav(dir);
    return;
  }
  _scrollView.onNav(dir);
}

void ST25R3916Screen::onRender() {
  if (_state == STATE_MENU || _state == STATE_MFC_MENU || _state == STATE_MFC_TAG_MENU ||
      _state == STATE_MFU_MENU || _state == STATE_MFU_TAG_MENU || _state == STATE_MFU_DUMP_SELECT ||
      _state == STATE_MFC_NDEF_MENU || _state == STATE_MFC_NDEF_WRITE_MENU ||
      _state == STATE_MFC_NDEF_FILE_SELECT || _state == STATE_MFC_DUMP_SELECT) {
    ListScreen::onRender();
    return;
  }
  if (_state == STATE_SCANNING || _state == STATE_MFC_READING || _state == STATE_MFU_READING || _state == STATE_MFC_NDEF_READING ||
      _state == STATE_MFC_NDEF_WRITING || _state == STATE_MFC_WRITING || _state == STATE_MFC_ERASING ||
      _state == STATE_MFU_WRITING || _state == STATE_MFU_ERASING) {
    _renderTagPrompt();
    return;
  }
  if (_state == STATE_MFC_DUMP_HEX) {
    _renderMfcDump();
    return;
  }
  if (_state == STATE_MFU_DUMP_HEX) {
    _renderMfuDump();
    return;
  }
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}

void ST25R3916Screen::onBack() {
  if (_state == STATE_DETAILS) {
    _showMenu();
    return;
  }
  if (_state == STATE_MFU_DUMP_HEX) {
    _state = STATE_MFU_DETAILS;
    render();
    return;
  }
  if (_state == STATE_MFU_DETAILS || _state == STATE_MFU_READING || _state == STATE_MFU_WRITE_PREVIEW ||
      _state == STATE_MFU_WRITING || _state == STATE_MFU_ERASING || _state == STATE_MFU_DUMP_SELECT) {
    _showMfuTagMenu();
    return;
  }
  if (_state == STATE_MFU_TAG_MENU) {
    _showMfuMenu();
    return;
  }
  if (_state == STATE_MFU_MENU) {
    _showMenu();
    return;
  }
  if (_state == STATE_MFC_DUMP_HEX) {
    _state = STATE_MFC_DETAILS;
    render();
    return;
  }
  if (_state == STATE_MFC_WRITE_PREVIEW || _state == STATE_MFC_DUMP_SELECT || _state == STATE_MFC_WRITING) {
    _showMfcTagMenu();
    return;
  }
  if (_state == STATE_MFC_NDEF_DETAILS) {
    if (_ndefWritePreview) {
      const bool fromFile = _ndefWritePreviewFromFile;
      _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
      if (fromFile) _openNdefFilePicker(); else _showMfcNdefWriteMenu();
    } else _showMfcNdefMenu();
    return;
  }
  if (_state == STATE_MFC_NDEF_READING || _state == STATE_MFC_NDEF_WRITING) { _showMfcNdefMenu(); return; }
  if (_state == STATE_MFC_NDEF_WRITE_MENU || _state == STATE_MFC_NDEF_FILE_SELECT) { _showMfcNdefMenu(); return; }
  if (_state == STATE_MFC_NDEF_MENU) {
    _showMfcMenu();
    return;
  }
  if (_state == STATE_MFC_DETAILS || _state == STATE_MFC_READING) {
    _showMfcTagMenu();
    return;
  }
  if (_state == STATE_MFC_TAG_MENU) {
    _showMfcMenu();
    return;
  }
  if (_state == STATE_MFC_MENU) {
    _showMenu();
    return;
  }
  Screen.goBack();
}

void ST25R3916Screen::onItemSelected(uint8_t index) {
#if defined(DEVICE_HAS_ST25R3916)
  if (_state == STATE_MFU_MENU) {
    if (index == 0) _showMfuTagMenu();
    return;
  }
  if (_state == STATE_MFU_TAG_MENU) {
    if (index == 0) _readMfuTag();
    else if (index == 1) _openMfuDumpPicker();
    else if (index == 2) _eraseMfuTag();
    return;
  }
  if (_state == STATE_MFU_DUMP_SELECT) {
    _openMfuDumpFile(index);
    return;
  }
  if (_state == STATE_MFC_MENU) {
    if (index == 0) _showMfcTagMenu();
    else if (index == 1) _showMfcNdefMenu();
    return;
  }
  if (_state == STATE_MFC_NDEF_MENU) {
    if (index == 0) _readMfcNdef();
    else if (index == 1) _showMfcNdefWriteMenu();
    else if (index == 2) _eraseMfcNdef();
    else if (index == 3) _formatMfc1kNdef();
    return;
  }
  if (_state == STATE_MFC_NDEF_WRITE_MENU) {
    if (index < 4) _writeNdefBuilt(index);
    else if (index == 4) _writeNdefVcard();
    else if (index == 5) _openNdefFilePicker();
    return;
  }
  if (_state == STATE_MFC_NDEF_FILE_SELECT) { _openNdefFile(index); return; }
  if (_state == STATE_MFC_TAG_MENU) {
    if (index == 0) _readMfcTag();
    else if (index == 1) _openMfcDumpPicker();
    else if (index == 2) _eraseMfcTag();
    return;
  }
  if (_state == STATE_MFC_DUMP_SELECT) {
    _openMfcDumpFile(index);
    return;
  }

  switch (index) {
    case 0: _scan(ST25R3916Backend::TECH_ALL); break;
    case 1: _showMfcMenu(); break;
    case 2: _showMfuMenu(); break;
    case 3: _showI2CInfo(); break;
    case 4: _showSPIInfo(); break;
  }
#else
  (void)index;
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_showMenu() {
  _state = STATE_MENU;
  setItems(_items, 5);
}

void ST25R3916Screen::_showMfcMenu() {
  _state = STATE_MFC_MENU;
  setItems(_mfcItems, 2);
  render();
}

void ST25R3916Screen::_showMfcTagMenu() {
  _state = STATE_MFC_TAG_MENU;
  setItems(_mfcTagItems, 3);
  render();
}

void ST25R3916Screen::_showMfcNdefMenu() {
  _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
  _state = STATE_MFC_NDEF_MENU;
  setItems(_mfcNdefItems, 4);
  render();
}

void ST25R3916Screen::_showMfcNdefWriteMenu() {
  _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
  _state = STATE_MFC_NDEF_WRITE_MENU;
  setItems(_mfcNdefWriteItems, 6);
  render();
}

void ST25R3916Screen::_showMfuMenu() {
  _state = STATE_MFU_MENU;
  setItems(_mfuItems, 1);
  render();
}

void ST25R3916Screen::_showMfuTagMenu() {
  _state = STATE_MFU_TAG_MENU;
  setItems(_mfuTagItems, 2);
  render();
}

void ST25R3916Screen::_renderTagPrompt() {
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);
}

void ST25R3916Screen::_scan(uint16_t techMask) {
#if defined(DEVICE_HAS_ST25R3916)
  _lastTechMask = techMask;
  const State previousState = _state;
  _state = STATE_SCANNING;
  render();

  ST25R3916Backend dev;
  const char* bus = nullptr;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (ready) {
    bus = "I2C";
  } else {
    ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
    if (ready) bus = "SPI";
  }

  if (!ready) {
    ShowStatusAction::show("ST25R3916 not found");
    if (previousState == STATE_DETAILS) {
      _state = STATE_DETAILS;
      render();
    } else {
      _showMenu();
    }
    return;
  }

  ST25R3916Backend::ScanResult result;
  if (!dev.scan(techMask, result, 1800)) {
    ShowStatusAction::show("No tag detected", 1200);
    if (previousState == STATE_DETAILS) {
      _state = STATE_DETAILS;
      render();
    } else {
      _showMenu();
    }
    return;
  }

  const char* tech = "Unknown";
  const char* protocol = "Unknown";
  switch (result.technology) {
    case ST25R3916Backend::Technology::NFC_A:
      tech = inferNfcAType(result.sak, result.atqa);
      protocol = "ISO14443A";
      break;
    case ST25R3916Backend::Technology::NFC_B:
      tech = "NFC-B";
      protocol = "ISO14443B";
      break;
    case ST25R3916Backend::Technology::NFC_F:
      tech = "NFC-F / FeliCa";
      protocol = "NFC-F";
      break;
    case ST25R3916Backend::Technology::NFC_V:
      tech = "NFC-V / ISO15693";
      protocol = "ISO15693";
      break;
    default:
      break;
  }

  String id;
  for (uint8_t i = 0; i < result.nfcidLen; i++) {
    char h[4];
    snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", result.nfcid[i]);
    id += h;
  }
  if (!id.length()) id = "--";

  _rowCount = 0;
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };

  addRow("Type", tech);
  addRow(result.technology == ST25R3916Backend::Technology::NFC_A ? "UID" : "NFCID", id);

  if (result.technology == ST25R3916Backend::Technology::NFC_A) {
    char atqa[8];
    snprintf(atqa, sizeof(atqa), "%02X:%02X", result.atqa[0], result.atqa[1]);
    addRow("ATQA", atqa);

    char sak[4];
    snprintf(sak, sizeof(sak), "%02X", result.sak);
    addRow("SAK", sak);
  }

  addRow(result.technology == ST25R3916Backend::Technology::NFC_A ? "UID Len" : "NFCID Len",
         String(result.nfcidLen) + " bytes");
  addRow("Protocol", protocol);
  if (result.isoDep) addRow("ISO-DEP", "Yes");
  addRow("Reader", bus ? bus : "--");
  addRow("[Press]", "Scan again");

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_DETAILS;
  render();
#endif
}

void ST25R3916Screen::_readMfcTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_READING;
  render();

  ST25R3916Backend dev;
  const char* bus = nullptr;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (ready) {
    bus = "I2C";
  } else {
    ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
    if (ready) bus = "SPI";
  }
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not found");
    _showMfcTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 3000, true)) {
    ShowStatusAction::show("No tag detected", 1200);
    _showMfcTagMenu();
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Not MIFARE Classic");
    _showMfcTagMenu();
    return;
  }

  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  if (!sectors || !blocks) {
    ShowStatusAction::show("Unsupported MIFARE Classic");
    _showMfcTagMenu();
    return;
  }

  const size_t dumpLen = blocks * 16U;
  if (dumpLen > sizeof(_mfcDump)) {
    ShowStatusAction::show("Dump too large");
    _showMfcTagMenu();
    return;
  }
  memset(_mfcDump, 0, sizeof(_mfcDump));
  _mfcDumpFromCompleteRead = false;
  _mfcDumpLen = dumpLen;
  _mfcDumpBlocks = (uint16_t)blocks;
  _mfcDumpOffset = 0;
  memset(_mfcUid, 0, sizeof(_mfcUid));
  _mfcUidLen = min((uint8_t)sizeof(_mfcUid), tag.nfcidLen);
  memcpy(_mfcUid, tag.nfcid, _mfcUidLen);
  _mfcSak = tag.sak;

  const auto defaults = NFCUtility::getDefaultKeys();
  bool blockSeen[256] = {};
  uint8_t sectorKnownKey[40][6] = {};
  bool sectorKnownKeyB[40] = {};
  bool sectorKnownKeyValid[40] = {};
  size_t sectorsWithKey = 0;
  size_t blocksRead = 0;

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 1200, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  ProgressView::init();
  for (size_t sector = 0; sector < sectors; ++sector) {
    char msg[42];
    snprintf(msg, sizeof(msg), "Reading sectors (%u/%u)...",
             (unsigned)(sector + 1), (unsigned)sectors);
    ProgressView::progress(msg, (int)(sector * 100U / sectors));

    const size_t first = sectorFirstBlock(sector);
    const size_t count = sectorBlockCount(sector);
    const uint8_t trailer = (uint8_t)(first + count - 1U);
    bool sectorHasKey = false;
    size_t sectorBlocksRead = 0;

    for (uint8_t keyType = 0; keyType < 2 && sectorBlocksRead < count; ++keyType) {
      for (const auto& candidate : defaults) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        const auto& key = candidate.value();
        if (!dev.mifareClassicAuthenticate(trailer, key.data(), keyType == 1)) {
          dev.deactivate();
          continue;
        }

        sectorHasKey = true;
        if (!sectorKnownKeyValid[sector]) {
          memcpy(sectorKnownKey[sector], key.data(), 6);
          sectorKnownKeyB[sector] = (keyType == 1);
          sectorKnownKeyValid[sector] = true;
        }
        for (size_t offset = 0; offset < count; ++offset) {
          const size_t block = first + offset;
          if (blockSeen[block]) continue;
          uint8_t data[16];
          if (!dev.mifareClassicReadBlock((uint8_t)block, data)) break;
          memcpy(_mfcDump + block * 16U, data, 16);
          blockSeen[block] = true;
          ++sectorBlocksRead;
          ++blocksRead;
        }
        dev.deactivate();
        if (sectorBlocksRead == count) break;
      }
    }
    if (sectorHasKey) ++sectorsWithKey;

    // Classic trailer reads do not expose Key A as ordinary memory, and Key B
    // may also be hidden by the access conditions. Reinsert the credential we
    // actually used so a complete dump does not silently replace a known key
    // with masked/zero bytes when it is later saved or written to another tag.
    if (sectorKnownKeyValid[sector] && blockSeen[trailer]) {
      uint8_t* trailerData = _mfcDump + (size_t)trailer * 16U;
      if (sectorKnownKeyB[sector]) memcpy(trailerData + 10, sectorKnownKey[sector], 6);
      else memcpy(trailerData, sectorKnownKey[sector], 6);
    }
  }
  ProgressView::finish();
  _mfcDumpFromCompleteRead = (blocksRead == blocks);

  String uid;
  for (uint8_t i = 0; i < tag.nfcidLen; ++i) {
    char h[4];
    snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", tag.nfcid[i]);
    uid += h;
  }

  _rowCount = 0;
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };

  addRow("Type", inferNfcAType(tag.sak, tag.atqa));
  addRow("UID", uid);
  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%02X:%02X", tag.atqa[0], tag.atqa[1]);
  addRow("ATQA", atqa);
  char sak[4];
  snprintf(sak, sizeof(sak), "%02X", tag.sak);
  addRow("SAK", sak);
  addRow("Dump", String((unsigned)_mfcDumpLen) + " bytes");
  addRow("Sectors", String((unsigned)sectors));
  addRow("Blocks", String((unsigned)blocksRead) + "/" + String((unsigned)blocks));
  addRow("Keys", String((unsigned)sectorsWithKey) + "/" + String((unsigned)sectors) + " sectors");
  addRow("Status", blocksRead == blocks ? "Complete" : "Partial");
  addRow("Reader", bus ? bus : "--");
  addRow("[Press]", "Actions");

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFC_DETAILS;
  render();
#endif
}

void ST25R3916Screen::_showMfcDumpActions() {
  if (!_mfcDumpLen || !_mfcDumpBlocks) {
    ShowStatusAction::show("No dump available");
    render();
    return;
  }

  static const InputSelectAction::Option opts[] = {
    {"View Dump", "view"},
    {"Save Dump", "save"},
    {"Write to Tag", "write"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 3, nullptr);
  if (!r) { render(); return; }

  render();
  if (strcmp(r, "view") == 0) {
    _mfcDumpOffset = 0;
    _state = STATE_MFC_DUMP_HEX;
    render();
  } else if (strcmp(r, "save") == 0) {
    _saveMfcDump();
  } else if (strcmp(r, "write") == 0) {
    _showMfcWritePreview(_mfcDump, _mfcDumpLen, false);
  }
}



void ST25R3916Screen::_openMfcDumpPicker() {
  _state = STATE_MFC_DUMP_SELECT;
  if (_dumpPickDir.length() == 0) _dumpPickDir = "/unigeek/nfc/dumps";
  _browser.root = "/unigeek/nfc/dumps";
  uint8_t n = _browser.load(this, _dumpPickDir, BrowseFileView::Mode(".bin", 320, 1024, 4096));
  if (n == 0 && _dumpPickDir == _browser.root) {
    ShowStatusAction::show("No compatible Classic .bin");
    _showMfcTagMenu();
    return;
  }
  setItems(_browser.items(), n);
}

void ST25R3916Screen::_openMfcDumpFile(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto& e = _browser.entry(index);
  if (e.isDir) {
    _dumpPickDir = e.path;
    _openMfcDumpPicker();
    return;
  }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    _showMfcTagMenu();
    return;
  }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) { ShowStatusAction::show("Open failed"); _showMfcTagMenu(); return; }
  const size_t len = f.size();
  if (len != 320 && len != 1024 && len != 4096) {
    f.close(); ShowStatusAction::show("Unsupported dump size"); _showMfcTagMenu(); return;
  }
  const size_t got = f.read(_mfcDump, len);
  f.close();
  if (got != len) { ShowStatusAction::show("Read failed"); _showMfcTagMenu(); return; }
  _mfcDumpLen = len;
  _mfcDumpBlocks = (uint16_t)(len / 16U);
  _mfcDumpFromCompleteRead = false;
  _showMfcWritePreview(_mfcDump, len, true);
}

void ST25R3916Screen::_showMfcWritePreview(const uint8_t* dump, size_t len, bool fromFile) {
  if (!dump || (len != 320 && len != 1024 && len != 4096)) {
    ShowStatusAction::show("Invalid dump");
    _showMfcTagMenu();
    return;
  }
  if (dump != _mfcDump) memcpy(_mfcDump, dump, len);
  _mfcDumpLen = len;
  _mfcDumpBlocks = (uint16_t)(len / 16U);
  _writePreviewFromFile = fromFile;
  memcpy(_writeSourceUid, _mfcDump, sizeof(_writeSourceUid));
  _writeSourceUidKnown = _mfcDump[4] == (uint8_t)(_writeSourceUid[0] ^ _writeSourceUid[1] ^
                                                  _writeSourceUid[2] ^ _writeSourceUid[3]);

  _rowCount = 0;
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };
  addRow("Source", "File");
  addRow("Type", len == 320 ? "MIFARE Classic Mini" :
                 (len == 4096 ? "MIFARE Classic 4K" : "MIFARE Classic 1K"));
  String uid = "Unknown";
  if (_writeSourceUidKnown) {
    uid = "";
    for (uint8_t i = 0; i < 4; ++i) {
      char h[4];
      snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", _writeSourceUid[i]);
      uid += h;
    }
  }
  addRow("UID", uid);
  addRow("UID Action", "Preserved");
  addRow("Blocks", String((unsigned)(len / 16U)));
  addRow("Dump", String((unsigned)len) + " bytes");
  addRow("[Press]", "Write to Tag");
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFC_WRITE_PREVIEW;
  render();
}

bool ST25R3916Screen::_writeMfcDumpToTag() {
#if defined(DEVICE_HAS_ST25R3916)
  if (_mfcDumpLen != 320 && _mfcDumpLen != 1024 && _mfcDumpLen != 4096) {
    ShowStatusAction::show("Invalid dump");
    _showMfcTagMenu();
    return false;
  }

  _state = STATE_MFC_WRITING;
  render();
  ST25R3916Backend dev;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (!ready) ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  if (!ready) { ShowStatusAction::show("ST25R3916 not found"); _showMfcTagMenu(); return false; }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) {
    ShowStatusAction::show("No tag detected"); _showMfcTagMenu(); return false;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Not MIFARE Classic"); _showMfcTagMenu(); return false;
  }
  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  if (blocks * 16U != _mfcDumpLen) {
    ShowStatusAction::show("Tag size mismatch"); _showMfcTagMenu(); return false;
  }

  const auto defaults = NFCUtility::getDefaultKeys();
  size_t written = 0;
  const size_t totalWritable = blocks > 0 ? blocks - 1U : 0;

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 1200, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  ProgressView::init();
  for (size_t sector = 0; sector < sectors; ++sector) {
    const size_t first = sectorFirstBlock(sector);
    const size_t count = sectorBlockCount(sector);
    const uint8_t trailer = (uint8_t)(first + count - 1U);
    for (size_t off = 0; off < count; ++off) {
      const size_t block = first + off;
      if (block == 0) continue;
      char msg[42];
      snprintf(msg, sizeof(msg), "Writing blocks (%u/%u)...",
               (unsigned)(written + 1U), (unsigned)totalWritable);
      ProgressView::progress(msg, totalWritable ? (int)(written * 100U / totalWritable) : 0);

      bool ok = false;
      for (uint8_t keyType = 0; keyType < 2 && !ok; ++keyType) {
        for (const auto& candidate : defaults) {
          if (!dev.hasActiveTag() && !reactivate()) continue;
          const auto& key = candidate.value();
          if (!dev.mifareClassicAuthenticate(trailer, key.data(), keyType == 1)) {
            dev.deactivate();
            continue;
          }
          ok = dev.mifareClassicWriteBlock((uint8_t)block, _mfcDump + block * 16U);
          dev.deactivate();
          if (ok) break;
        }
      }
      if (!ok) {
        ProgressView::finish();
        ShowStatusAction::show("Write failed");
        _showMfcTagMenu();
        return false;
      }
      ++written;
    }
  }
  ProgressView::finish();
  ShowStatusAction::show("Write complete", 1500);
  _showMfcTagMenu();
  return true;
#else
  return false;
#endif
}


void ST25R3916Screen::_eraseMfcTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_ERASING;
  render();

  ST25R3916Backend dev;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (!ready) ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not found");
    _showMfcTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) {
    ShowStatusAction::show("No tag detected", 1200);
    _showMfcTagMenu();
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Not MIFARE Classic");
    _showMfcTagMenu();
    return;
  }

  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  if (!sectors || !blocks || sectors > 40) {
    ShowStatusAction::show("Unsupported MIFARE Classic");
    _showMfcTagMenu();
    return;
  }

  const auto defaults = NFCUtility::getDefaultKeys();
  uint8_t sectorKeys[40][6] = {};
  bool sectorUseKeyB[40] = {};
  bool sectorKeyFound[40] = {};

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 1200, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  // Probe every sector first. Do not start erasing unless all sectors can be
  // authenticated with one of the standard keys, matching the CU/PN532 UX.
  ProgressView::init();
  bool keysOk = true;
  for (size_t sector = 0; sector < sectors; ++sector) {
    char msg[40];
    snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...",
             (unsigned)(sector + 1U), (unsigned)sectors);
    ProgressView::progress(msg, (int)(sector * 100U / sectors));

    const size_t first = sectorFirstBlock(sector);
    const size_t count = sectorBlockCount(sector);
    const uint8_t trailer = (uint8_t)(first + count - 1U);

    for (uint8_t keyType = 0; keyType < 2 && !sectorKeyFound[sector]; ++keyType) {
      for (const auto& candidate : defaults) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        const auto& key = candidate.value();
        if (dev.mifareClassicAuthenticate(trailer, key.data(), keyType == 1)) {
          memcpy(sectorKeys[sector], key.data(), 6);
          sectorUseKeyB[sector] = (keyType == 1);
          sectorKeyFound[sector] = true;
          dev.deactivate();
          break;
        }
        dev.deactivate();
      }
    }
    if (!sectorKeyFound[sector]) {
      keysOk = false;
      break;
    }
  }
  ProgressView::finish();

  if (!keysOk) {
    ShowStatusAction::show("Erase failed: missing key", 1700);
    _showMfcTagMenu();
    return;
  }

  uint8_t zero[16] = {};
  const size_t totalDataBlocks = blocks - sectors - 1U; // exclude trailers + manufacturer block
  size_t erased = 0;

  ProgressView::init();
  for (size_t sector = 0; sector < sectors; ++sector) {
    const size_t first = sectorFirstBlock(sector);
    const size_t count = sectorBlockCount(sector);
    const uint8_t trailer = (uint8_t)(first + count - 1U);

    for (size_t off = 0; off + 1U < count; ++off) {
      const size_t block = first + off;
      if (block == 0) continue; // preserve manufacturer block / UID

      char msg[40];
      snprintf(msg, sizeof(msg), "Erasing blocks (%u/%u)...",
               (unsigned)(erased + 1U), (unsigned)totalDataBlocks);
      ProgressView::progress(msg, totalDataBlocks ? (int)(erased * 100U / totalDataBlocks) : 0);

      if (!dev.hasActiveTag() && !reactivate()) {
        ProgressView::finish();
        ShowStatusAction::show("Erase failed");
        _showMfcTagMenu();
        return;
      }
      if (!dev.mifareClassicAuthenticate(trailer, sectorKeys[sector], sectorUseKeyB[sector])) {
        dev.deactivate();
        ProgressView::finish();
        ShowStatusAction::show("Erase failed");
        _showMfcTagMenu();
        return;
      }
      const bool ok = dev.mifareClassicWriteBlock((uint8_t)block, zero);
      dev.deactivate();
      if (!ok) {
        ProgressView::finish();
        ShowStatusAction::show("Erase failed");
        _showMfcTagMenu();
        return;
      }
      ++erased;
    }
  }
  ProgressView::finish();

  // A successful erase invalidates the cached "last read" dump so it cannot
  // accidentally be offered as a write source after the tag contents changed.
  _mfcDumpLen = 0;
  _mfcDumpBlocks = 0;
  _mfcDumpFromCompleteRead = false;
  _mfcUidLen = 0;
  _mfcSak = 0;

  ShowStatusAction::show("Tag erased", 1600);
  _showMfcTagMenu();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_saveMfcDump() {
  if (!_mfcDumpLen || !_mfcUidLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Save failed", 1200);
    render();
    return;
  }

  const char* typeName = (_mfcSak == 0x09) ? "MF-Mini"
                       : (_mfcSak == 0x18) ? "MF-4K"
                                           : "MF-1K";
  String suggested = String(typeName) + "_";
  for (uint8_t i = 0; i < _mfcUidLen; ++i) {
    char h[3];
    snprintf(h, sizeof(h), "%02X", _mfcUid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("Save dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }
  render();

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  const String filename = name + ".bin";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  const String path = String("/unigeek/nfc/dumps/") + filename;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (f) {
    ok = f.write(_mfcDump, _mfcDumpLen) == _mfcDumpLen;
    f.close();
  }

  render();
  if (ok) {
    const String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1500);
  } else {
    ShowStatusAction::show("Save failed", 1200);
  }
  render();
}

void ST25R3916Screen::_renderMfcDump() {
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  static constexpr int kRowH = 14;
  static constexpr int kScrollW = 3;
  const int fullyVisible = max(1, bh / kRowH);
  const int visible = fullyVisible + ((bh % kRowH >= 5) ? 1 : 0);
  const uint16_t totalRows = _mfcDumpBlocks * 2U;
  const int textW = bw - kScrollW - 4;

  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  for (int i = 0; i < visible; ++i) {
    const uint16_t row = _mfcDumpOffset + (uint16_t)i;
    if (row >= totalRows) break;
    const uint16_t block = row / 2U;
    const uint8_t half = row & 1U;
    const size_t off = (size_t)block * 16U + half * 8U;

    char label[16];
    snprintf(label, sizeof(label), "B%u %s", (unsigned)block, half ? "8-F" : "0-7");
    char value[24];
    size_t valuePos = 0;
    for (uint8_t b = 0; b < 8; ++b) {
      valuePos += snprintf(value + valuePos, sizeof(value) - valuePos,
                           "%02X%s", _mfcDump[off + b], b == 7 ? "" : " ");
    }

    const int rowY = by + i * kRowH;
    const int rowH = min(kRowH, by + bh - rowY);
    if (rowH <= 0) break;
    Sprite sp(&lcd);
    sp.createSprite(bw - kScrollW, rowH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_DARKGREY);
    sp.drawString(label, 2, 3);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_WHITE);
    sp.drawString(value, textW, 3);
    sp.pushSprite(bx, rowY);
    sp.deleteSprite();
  }

  const int sbX = bx + bw - kScrollW;
  lcd.fillRect(sbX, by, kScrollW, bh, 0x2104);
  if (totalRows > (uint16_t)fullyVisible) {
    const int sbH = max(4, bh * fullyVisible / (int)totalRows);
    const uint16_t maxOffset = totalRows - fullyVisible;
    const int sbY = (bh - sbH) * _mfcDumpOffset / max(1, (int)maxOffset);
    lcd.fillRect(sbX, by + sbY, kScrollW, sbH, Config.getThemeColor());
  }
}

void ST25R3916Screen::_handleMfcDumpNav(INavigation::Direction dir) {
  const uint16_t totalRows = _mfcDumpBlocks * 2U;
  const int visible = max(1, bodyH() / 14);
  const uint16_t maxOffset = totalRows > (uint16_t)visible ? totalRows - visible : 0;

  if (dir == INavigation::DIR_UP && _mfcDumpOffset > 0) {
    --_mfcDumpOffset;
  } else if (dir == INavigation::DIR_DOWN && _mfcDumpOffset < maxOffset) {
    ++_mfcDumpOffset;
  } else if (dir == INavigation::DIR_LEFT && _mfcDumpOffset > 0) {
    _mfcDumpOffset = _mfcDumpOffset > (uint16_t)visible ? _mfcDumpOffset - visible : 0;
  } else if (dir == INavigation::DIR_RIGHT && _mfcDumpOffset < maxOffset) {
    _mfcDumpOffset = (uint16_t)min((int)maxOffset, (int)_mfcDumpOffset + visible);
  } else {
    return;
  }
  _renderMfcDump();
}


void ST25R3916Screen::_addWrappedRow(const String& label, const String& value) {
  static constexpr size_t kChunk = 24;
  if (!value.length()) return;
  size_t pos = 0;
  bool first = true;
  while (pos < value.length() && _rowCount < kMaxRows) {
    size_t take = min(kChunk, value.length() - pos);
    _rowLabels[_rowCount] = first ? label : "";
    _rowValues[_rowCount] = value.substring(pos, pos + take);
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    ++_rowCount;
    pos += take;
    first = false;
  }
}

void ST25R3916Screen::_showNdefDetails(const uint8_t* uid, uint8_t uidLen,
                                       const uint8_t* ndef, size_t ndefLen) {
  _rowCount = 0;
  _hasNdef = false;
  _ndefLen = 0;

  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    ++_rowCount;
  };

  if (uid && uidLen) {
    String id;
    for (uint8_t i = 0; i < uidLen; ++i) {
      char h[4];
      snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", uid[i]);
      id += h;
    }
    addRow("UID", id);
  }

  if (!ndef) {
    addRow("NDEF", "Not found");
  } else {
    if (ndefLen <= sizeof(_ndefBuf)) {
      memcpy(_ndefBuf, ndef, ndefLen);
      _ndefLen = ndefLen;
      _hasNdef = true;
    }
    addRow("NDEF Size", String((unsigned)ndefLen) + " bytes");
    if (_ndefCapacity) {
      addRow("Capacity", String((unsigned)_ndefCapacity) + " bytes");
      addRow("Free", String((unsigned)(_ndefCapacity > ndefLen ? _ndefCapacity - ndefLen : 0)) + " bytes");
    }

    if (ndefLen == 0) {
      addRow("NDEF", "Empty");
    } else {
      NdefParser::Result parsed;
      if (!NdefParser::parse(ndef, ndefLen, parsed)) {
        addRow("NDEF", "Invalid record");
      } else {
        switch (parsed.kind) {
          case NdefParser::RECORD_TEXT:
            addRow("Record", "Text");
            if (parsed.language.length()) addRow("Language", parsed.language);
            _addWrappedRow("Text", parsed.text);
            break;
          case NdefParser::RECORD_URL:
            addRow("Record", "URL");
            _addWrappedRow("URL", parsed.uri);
            break;
          case NdefParser::RECORD_PHONE:
            addRow("Record", "Phone");
            _addWrappedRow("Phone", parsed.phone);
            break;
          case NdefParser::RECORD_EMAIL:
            addRow("Record", "Email");
            _addWrappedRow("Email", parsed.email);
            break;
          case NdefParser::RECORD_VCARD:
            addRow("Record", "vCard");
            if (parsed.contact.length()) _addWrappedRow("Contact", parsed.contact);
            if (parsed.company.length()) _addWrappedRow("Company", parsed.company);
            if (parsed.address.length()) _addWrappedRow("Address", parsed.address);
            if (parsed.phone.length()) _addWrappedRow("Phone", parsed.phone);
            if (parsed.email.length()) _addWrappedRow("Email", parsed.email);
            if (parsed.website.length()) _addWrappedRow("Website", parsed.website);
            break;
          default:
            addRow("Record", "Unsupported");
            break;
        }
      }
    }
  }

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFC_NDEF_DETAILS;
  render();
}


void ST25R3916Screen::_showNdefWritePreview(const uint8_t* ndef, size_t ndefLen, bool fromFile) {
  if (!ndef || !ndefLen || ndefLen > kMaxNdefBytes) { ShowStatusAction::show("Invalid NDEF"); _showMfcNdefWriteMenu(); return; }
  memcpy(_ndefBuf, ndef, ndefLen); _ndefLen = ndefLen; _hasNdef = true;
  _ndefWritePreview = true; _ndefWritePreviewFromFile = fromFile; _ndefCapacity = 0;
  _showNdefDetails(nullptr, 0, _ndefBuf, _ndefLen);
  if (_rowCount < kMaxRows) {
    _rowLabels[_rowCount] = "[Press]"; _rowValues[_rowCount] = "Write to Tag";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]}; ++_rowCount;
    _scrollView.setRows(_rows, _rowCount); render();
  }
}

void ST25R3916Screen::_writeNdefBuilt(uint8_t kind) {
  const char* label = kind == 0 ? "Text" : kind == 1 ? "URL" : kind == 2 ? "Phone" : "Email";
  String initial = kind == 1 ? "https://" : "";
  String value = InputTextAction::popup(label, initial, kind == 2 ? InputTextAction::INPUT_PHONE : InputTextAction::INPUT_TEXT);
  if (InputTextAction::wasCancelled() || !value.length()) { _showMfcNdefWriteMenu(); return; }
  uint8_t b[kMaxNdefBytes] = {}; size_t n = 0;
  bool ok = kind == 0 ? NdefBuilder::buildText(value, b, n, sizeof(b)) :
            kind == 1 ? NdefBuilder::buildUrl(value, b, n, sizeof(b)) :
            kind == 2 ? NdefBuilder::buildPhone(value, b, n, sizeof(b)) :
                        NdefBuilder::buildEmail(value, b, n, sizeof(b));
  if (!ok) { ShowStatusAction::show("NDEF too large"); _showMfcNdefWriteMenu(); return; }
  _showNdefWritePreview(b, n, false);
}

void ST25R3916Screen::_writeNdefVcard() {
  String contact = InputTextAction::popup("Contact name", ""); if (InputTextAction::wasCancelled() || !contact.length()) { _showMfcNdefWriteMenu(); return; }
  String company = InputTextAction::popup("Company", ""); if (InputTextAction::wasCancelled()) { _showMfcNdefWriteMenu(); return; }
  String address = InputTextAction::popup("Address", ""); if (InputTextAction::wasCancelled()) { _showMfcNdefWriteMenu(); return; }
  String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE); if (InputTextAction::wasCancelled()) { _showMfcNdefWriteMenu(); return; }
  String email = InputTextAction::popup("Mail", ""); if (InputTextAction::wasCancelled()) { _showMfcNdefWriteMenu(); return; }
  String website = InputTextAction::popup("Website", "https://"); if (InputTextAction::wasCancelled()) { _showMfcNdefWriteMenu(); return; }
  uint8_t b[kMaxNdefBytes] = {}; size_t n = 0;
  if (!NdefBuilder::buildVcard(contact, company, address, phone, email, website, b, n, sizeof(b))) {
    ShowStatusAction::show("vCard too large"); _showMfcNdefWriteMenu(); return;
  }
  _showNdefWritePreview(b, n, false);
}

void ST25R3916Screen::_openNdefFilePicker() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { ShowStatusAction::show("Storage unavailable"); _showMfcNdefWriteMenu(); return; }
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir("/unigeek/nfc"); Uni.Storage->makeDir("/unigeek/nfc/ndefs");
  if (!_ndefPickDir.startsWith("/unigeek/nfc/ndefs")) _ndefPickDir = "/unigeek/nfc/ndefs";
  _browser.root = "/unigeek/nfc/ndefs"; _state = STATE_MFC_NDEF_FILE_SELECT;
  uint8_t n = _browser.load(this, _ndefPickDir, BrowseFileView::Mode(".ndef", 1, kMaxNdefBytes));
  if (!n && _ndefPickDir == _browser.root) { ShowStatusAction::show("No NDEF files"); _showMfcNdefWriteMenu(); return; }
  setItems(_browser.items(), n); render();
}

void ST25R3916Screen::_openNdefFile(uint8_t index) {
  if (index >= _browser.count()) return; const auto& e = _browser.entry(index);
  if (e.isDir) { _ndefPickDir = e.path; _openNdefFilePicker(); return; }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r"); if (!f || f.size() == 0 || f.size() > kMaxNdefBytes) { if (f) f.close(); ShowStatusAction::show("Invalid NDEF file"); _openNdefFilePicker(); return; }
  size_t n = f.size(); uint8_t b[kMaxNdefBytes] = {}; bool ok = f.read(b, n) == (int)n; f.close();
  if (!ok) { ShowStatusAction::show("Read failed"); _openNdefFilePicker(); return; }
  _showNdefWritePreview(b, n, true);
}

bool ST25R3916Screen::_writeMfcNdef(const uint8_t* ndef, size_t ndefLen) {
#if defined(DEVICE_HAS_ST25R3916)
  if (!ndef || !ndefLen || ndefLen > kMaxNdefBytes) { ShowStatusAction::show("NDEF too large"); return false; }
  _state = STATE_MFC_NDEF_WRITING; render();
  ST25R3916Backend dev; bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (!ready) ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  if (!ready) { ShowStatusAction::show("ST25R3916 not found"); return false; }
  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) { ShowStatusAction::show("No tag detected"); return false; }
  if (!isMifareClassic(tag.sak)) { ShowStatusAction::show("Not MIFARE Classic"); return false; }
  size_t totalSectors=0,totalBlocks=0; mfcDimensions(tag.sak,totalSectors,totalBlocks);
  auto reactivate=[&](){ dev.deactivate(); ST25R3916Backend::ScanResult cur; return dev.scan(ST25R3916Backend::TECH_A,cur,1200,true)&&sameTag(tag,cur); };
  static const uint8_t madKeys[][6]={{0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},{0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}};
  auto readMad=[&](uint8_t block,uint8_t out[16]){ uint8_t sec=block<128?block/4:(uint8_t)(32+(block-128)/16); uint8_t trailer=(uint8_t)(sectorFirstBlock(sec)+sectorBlockCount(sec)-1); for(auto& key:madKeys) for(uint8_t kt=0;kt<2;++kt){ if(!dev.hasActiveTag()&&!reactivate()) continue; if(dev.mifareClassicAuthenticate(trailer,key,kt==1)&&dev.mifareClassicReadBlock(block,out)){dev.deactivate();return true;} dev.deactivate(); } return false; };
  uint8_t sectors[39]={}; size_t sectorCount=0; auto add=[&](uint8_t sec,uint8_t a,uint8_t c){ if(sec<totalSectors&&sectorCount<sizeof(sectors)&&((a==0x03&&c==0xE1)||(a==0xE1&&c==0x03))) sectors[sectorCount++]=sec; };
  uint8_t b1[16]={},b2[16]={}; if(!readMad(1,b1)||!readMad(2,b2)){ShowStatusAction::show("Not NDEF formatted");return false;}
  for(uint8_t sec=1;sec<=7;++sec){size_t o=2+(sec-1)*2;add(sec,b1[o],b1[o+1]);} for(uint8_t sec=8;sec<=15;++sec){size_t o=(sec-8)*2;add(sec,b2[o],b2[o+1]);}
  if(totalSectors>16){uint8_t m0[16]={},m1[16]={},m2[16]={}; if(readMad(64,m0)&&readMad(65,m1)&&readMad(66,m2)){for(uint8_t sec=17;sec<=23;++sec){size_t o=2+(sec-17)*2;add(sec,m0[o],m0[o+1]);}for(uint8_t sec=24;sec<=31;++sec){size_t o=(sec-24)*2;add(sec,m1[o],m1[o+1]);}for(uint8_t sec=32;sec<=39;++sec){size_t o=(sec-32)*2;add(sec,m2[o],m2[o+1]);}}}
  if(!sectorCount){ShowStatusAction::show("Not NDEF formatted");return false;}
  size_t capacity=0; for(size_t i=0;i<sectorCount;++i) capacity += sectors[i]<32?48:240;
  size_t payloadLen=ndefLen+3; if(payloadLen>capacity){ShowStatusAction::show("NDEF does not fit");return false;}
  uint8_t* payload=new uint8_t[payloadLen]; if(!payload){ShowStatusAction::show("Out of memory");return false;} payload[0]=0x03;payload[1]=(uint8_t)ndefLen;memcpy(payload+2,ndef,ndefLen);payload[2+ndefLen]=0xFE;
  static const uint8_t keyA[6]={0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}; size_t off=0; bool success=true; uint8_t firstFinal[16]={},firstStaged[16]={}; uint8_t firstBlockNo=0; bool haveFirst=false;
  ProgressView::init();
  size_t totalData=0; for(size_t i=0;i<sectorCount;++i) totalData+=sectors[i]<32?3:15; size_t done=0;
  for(size_t si=0;si<sectorCount&&off<payloadLen&&success;++si){ uint8_t sec=sectors[si]; size_t first=sectorFirstBlock(sec); uint8_t dataBlocks=sec<32?3:15; uint8_t trailer=(uint8_t)(first+sectorBlockCount(sec)-1);
    for(uint8_t bi=0;bi<dataBlocks&&off<payloadLen&&success;++bi){ uint8_t blockNo=(uint8_t)(first+bi); char msg[40]; snprintf(msg,sizeof(msg),"Writing blocks (%u/%u)...",(unsigned)(done+1),(unsigned)totalData);ProgressView::progress(msg,totalData?(int)(done*100/totalData):0);
      if(!dev.hasActiveTag()&&!reactivate()){success=false;break;} if(!dev.mifareClassicAuthenticate(trailer,keyA,false)){dev.deactivate();success=false;break;} uint8_t block[16]={}; if(!dev.mifareClassicReadBlock(blockNo,block)){dev.deactivate();success=false;break;} dev.deactivate();
      size_t take=min((size_t)16,payloadLen-off); memcpy(block,payload+off,take); if(!haveFirst){memcpy(firstFinal,block,16);memcpy(firstStaged,block,16);firstStaged[1]=0x00;firstBlockNo=blockNo;haveFirst=true;memcpy(block,firstStaged,16);} if(!dev.hasActiveTag()&&!reactivate()){success=false;break;} if(!dev.mifareClassicAuthenticate(trailer,keyA,false)||!dev.mifareClassicWriteBlock(blockNo,block)){dev.deactivate();success=false;break;} dev.deactivate(); off+=take;++done;
    }
  }
  if(success&&haveFirst){uint8_t sec=firstBlockNo<128?firstBlockNo/4:(uint8_t)(32+(firstBlockNo-128)/16);uint8_t trailer=(uint8_t)(sectorFirstBlock(sec)+sectorBlockCount(sec)-1);if(!dev.hasActiveTag()&&!reactivate())success=false;else if(!dev.mifareClassicAuthenticate(trailer,keyA,false)||!dev.mifareClassicWriteBlock(firstBlockNo,firstFinal))success=false;dev.deactivate();}
  ProgressView::finish(); delete[] payload; ShowStatusAction::show(success?"NDEF written":"NDEF write failed"); return success;
#else
  return false;
#endif
}

void ST25R3916Screen::_eraseMfcNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_NDEF_WRITING;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (!ready) ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  if (!ready) { ShowStatusAction::show("ST25R3916 not found"); _showMfcNdefMenu(); return; }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) {
    ShowStatusAction::show("No tag detected"); _showMfcNdefMenu(); return;
  }
  if (!isMifareClassic(tag.sak)) {
    dev.deactivate(); ShowStatusAction::show("Not MIFARE Classic"); _showMfcNdefMenu(); return;
  }

  size_t totalSectors = 0, totalBlocks = 0;
  mfcDimensions(tag.sak, totalSectors, totalBlocks);
  uint8_t uid[10] = {};
  const uint8_t uidLen = min((uint8_t)sizeof(uid), tag.nfcidLen);
  memcpy(uid, tag.nfcid, uidLen);

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 1200, true) &&
           current.nfcidLen == uidLen && memcmp(current.nfcid, uid, uidLen) == 0;
  };

  static const uint8_t madKeys[][6] = {
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  };
  auto readMadBlock = [&](uint8_t block, uint8_t out[16]) -> bool {
    const uint8_t sector = block < 128 ? (uint8_t)(block / 4U)
                                       : (uint8_t)(32U + (block - 128U) / 16U);
    const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
    for (const auto& key : madKeys) {
      for (uint8_t keyType = 0; keyType < 2; ++keyType) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        if (dev.mifareClassicAuthenticate(trailer, key, keyType == 1) &&
            dev.mifareClassicReadBlock(block, out)) {
          dev.deactivate();
          return true;
        }
        dev.deactivate();
      }
    }
    return false;
  };

  uint8_t ndefSectors[39] = {};
  size_t ndefSectorCount = 0;
  auto addIfNdef = [&](uint8_t sector, uint8_t application, uint8_t cluster) {
    if (sector >= totalSectors || ndefSectorCount >= sizeof(ndefSectors)) return;
    if ((application == 0x03 && cluster == 0xE1) ||
        (application == 0xE1 && cluster == 0x03)) {
      ndefSectors[ndefSectorCount++] = sector;
    }
  };

  uint8_t b1[16] = {}, b2[16] = {};
  if (!readMadBlock(1, b1) || !readMadBlock(2, b2)) {
    ShowStatusAction::show("Not NDEF formatted"); _showMfcNdefMenu(); return;
  }
  for (uint8_t sec = 1; sec <= 7; ++sec) {
    const size_t off = 2U + (size_t)(sec - 1U) * 2U;
    addIfNdef(sec, b1[off], b1[off + 1U]);
  }
  for (uint8_t sec = 8; sec <= 15; ++sec) {
    const size_t off = (size_t)(sec - 8U) * 2U;
    addIfNdef(sec, b2[off], b2[off + 1U]);
  }
  if (totalSectors > 16) {
    uint8_t m0[16] = {}, m1[16] = {}, m2[16] = {};
    if (readMadBlock(64, m0) && readMadBlock(65, m1) && readMadBlock(66, m2)) {
      for (uint8_t sec = 17; sec <= 23; ++sec) {
        const size_t off = 2U + (size_t)(sec - 17U) * 2U;
        addIfNdef(sec, m0[off], m0[off + 1U]);
      }
      for (uint8_t sec = 24; sec <= 31; ++sec) {
        const size_t off = (size_t)(sec - 24U) * 2U;
        addIfNdef(sec, m1[off], m1[off + 1U]);
      }
      for (uint8_t sec = 32; sec <= 39; ++sec) {
        const size_t off = (size_t)(sec - 32U) * 2U;
        addIfNdef(sec, m2[off], m2[off + 1U]);
      }
    }
  }
  if (!ndefSectorCount) {
    ShowStatusAction::show("Not NDEF formatted"); _showMfcNdefMenu(); return;
  }

  static const uint8_t nfcKey[6] = {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7};
  const uint8_t sector = ndefSectors[0];
  const uint8_t block = (uint8_t)sectorFirstBlock(sector);
  const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
  uint8_t data[16] = {};

  if (!dev.hasActiveTag() && !reactivate()) {
    ShowStatusAction::show("NDEF sector locked"); _showMfcNdefMenu(); return;
  }
  if (!dev.mifareClassicAuthenticate(trailer, nfcKey, false) ||
      !dev.mifareClassicReadBlock(block, data)) {
    dev.deactivate(); ShowStatusAction::show("NDEF sector locked"); _showMfcNdefMenu(); return;
  }
  dev.deactivate();

  // Logical NDEF erase: empty NDEF Message TLV followed by Terminator TLV.
  // Bytes after FE remain untouched and are no longer part of the active NDEF.
  data[0] = 0x03;
  data[1] = 0x00;
  data[2] = 0xFE;

  if (!reactivate() || !dev.mifareClassicAuthenticate(trailer, nfcKey, false) ||
      !dev.mifareClassicWriteBlock(block, data)) {
    dev.deactivate(); ShowStatusAction::show("NDEF erase failed"); _showMfcNdefMenu(); return;
  }
  dev.deactivate();
  _hasNdef = false;
  _ndefLen = 0;
  ShowStatusAction::show("NDEF erased");
  _showMfcNdefMenu();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

bool ST25R3916Screen::_formatMfc1kNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_NDEF_WRITING; render(); _renderTagPrompt();
  ST25R3916Backend dev; bool ready=dev.beginI2C(Uni.ExI2C,ST25R3916_I2C_ADDR); if(!ready) ready=dev.beginSPI(Uni.Spi,ST25R3916_CS_PIN,ST25R3916_IRQ_PIN,ST25R3916_SPI_HZ);
  if(!ready){ShowStatusAction::show("ST25R3916 not found");_showMfcNdefMenu();return false;}
  ST25R3916Backend::ScanResult tag; if(!dev.scan(ST25R3916Backend::TECH_A,tag,5000,true)){ShowStatusAction::show("No tag detected");_showMfcNdefMenu();return false;}
  if(tag.sak!=0x08){dev.deactivate();ShowStatusAction::show("Format supports Classic 1K");_showMfcNdefMenu();return false;}
  uint8_t uid[10]={}; const uint8_t uidLen=min((uint8_t)sizeof(uid),tag.nfcidLen); memcpy(uid,tag.nfcid,uidLen);
  auto reactivate=[&](){ST25R3916Backend::ScanResult t;if(!dev.scan(ST25R3916Backend::TECH_A,t,600,true))return false;return t.nfcidLen==uidLen&&!memcmp(t.nfcid,uid,uidLen);};
  const auto defaults=NFCUtility::getDefaultKeys();
  uint8_t sectorKey[3][6]={}; bool sectorKeyB[3]={};
  for(uint8_t sec=0;sec<3;++sec){bool found=false;uint8_t trailer=(uint8_t)(sec*4+3);for(uint8_t kt=0;kt<2&&!found;++kt)for(const auto& c:defaults){const auto& k=c.value();if(!dev.hasActiveTag()&&!reactivate())continue;if(dev.mifareClassicAuthenticate(trailer,k.data(),kt==1)){memcpy(sectorKey[sec],k.data(),6);sectorKeyB[sec]=kt==1;found=true;}dev.deactivate();if(found)break;}if(!found){ShowStatusAction::show("Format: unknown sector key");_showMfcNdefMenu();return false;}}
  uint8_t madPayload[31]={}; madPayload[0]=0x01; madPayload[1]=0x03;madPayload[2]=0xE1;madPayload[3]=0x03;madPayload[4]=0xE1;
  auto crc8=[](const uint8_t* d,size_t n){uint8_t c=0xC7;for(size_t i=0;i<n;++i){c^=d[i];for(uint8_t b=0;b<8;++b)c=(c&0x80)?(uint8_t)((c<<1)^0x1D):(uint8_t)(c<<1);}return c;};
  uint8_t mad1[16]={},mad2[16]={};mad1[0]=crc8(madPayload,31);memcpy(mad1+1,madPayload,15);memcpy(mad2,madPayload+15,16);
  static const uint8_t madTrailer[16]={0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0x78,0x77,0x88,0xC1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  static const uint8_t nfcTrailer[16]={0xD3,0xF7,0xD3,0xF7,0xD3,0xF7,0x7F,0x07,0x88,0x40,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  uint8_t zero[16]={}; uint8_t empty[16]={0x03,0x00,0xFE};
  auto writeKnown=[&](uint8_t block,const uint8_t data[16]){uint8_t sec=block/4;for(uint8_t pass=0;pass<2;++pass){bool kb=pass==0?true:sectorKeyB[sec];const uint8_t* key=pass==0?sectorKey[sec]:sectorKey[sec];if(!dev.hasActiveTag()&&!reactivate())continue;if(dev.mifareClassicAuthenticate((uint8_t)(sec*4+3),key,kb)&&dev.mifareClassicWriteBlock(block,data)){dev.deactivate();return true;}dev.deactivate();}for(uint8_t kt=0;kt<2;++kt)for(const auto& c:defaults){const auto& k=c.value();if(!dev.hasActiveTag()&&!reactivate())continue;if(dev.mifareClassicAuthenticate((uint8_t)(sec*4+3),k.data(),kt==1)&&dev.mifareClassicWriteBlock(block,data)){dev.deactivate();return true;}dev.deactivate();}return false;};
  ProgressView::init(); bool ok=true; uint8_t done=0; const uint8_t total=11;
  auto put=[&](uint8_t b,const uint8_t d[16]){char msg[40];snprintf(msg,sizeof(msg),"Formatting blocks (%u/%u)...",(unsigned)(done+1),(unsigned)total);ProgressView::progress(msg,(int)((uint16_t)done*100/total));bool r=writeKnown(b,d);if(r)++done;return r;};
  ok=put(1,mad1)&&put(2,mad2); for(uint8_t sec=1;sec<=2&&ok;++sec)for(uint8_t bi=0;bi<3&&ok;++bi)ok=put((uint8_t)(sec*4+bi),(sec==1&&bi==0)?empty:zero); if(ok)ok=put(3,madTrailer);if(ok)ok=put(7,nfcTrailer);if(ok)ok=put(11,nfcTrailer);
  if(ok)ProgressView::progress("Format complete",100);ProgressView::finish();ShowStatusAction::show(ok?"NDEF formatted":"NDEF format failed");_showMfcNdefMenu();return ok;
#else
  ShowStatusAction::show("ST25R3916 not supported"); return false;
#endif
}


void ST25R3916Screen::_showMfuDumpActions() {
  if (!_mfuDumpLen || !_mfuPages) {
    ShowStatusAction::show("No dump available");
    render();
    return;
  }

  static const InputSelectAction::Option opts[] = {
    {"View Dump", "view"},
    {"Save Dump", "save"},
    {"Write to Tag", "write"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 3, nullptr);
  if (!r) { render(); return; }

  render();
  if (strcmp(r, "view") == 0) {
    _mfuDumpOffset = 0;
    _state = STATE_MFU_DUMP_HEX;
    render();
  } else if (strcmp(r, "save") == 0) {
    _saveMfuDump();
  } else if (strcmp(r, "write") == 0) {
    if (_mfuType != "NTAG215" || _mfuPages != 135 || _mfuDumpLen != 540) {
      ShowStatusAction::show("Write supports NTAG215", 1400);
      render();
      return;
    }
    _showMfuWritePreview(false);
  }
}


void ST25R3916Screen::_openMfuDumpPicker() {
  _state = STATE_MFU_DUMP_SELECT;
  if (_dumpPickDir.length() == 0) _dumpPickDir = "/unigeek/nfc/dumps";
  _browser.root = "/unigeek/nfc/dumps";
  uint8_t n = _browser.load(this, _dumpPickDir, BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 540));
  if (n == 0 && _dumpPickDir == _browser.root) {
    ShowStatusAction::show("No NTAG215 .bin", 1500);
    _showMfuTagMenu();
    return;
  }
  setItems(_browser.items(), n);
}

void ST25R3916Screen::_openMfuDumpFile(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto& e = _browser.entry(index);
  if (e.isDir) { _dumpPickDir = e.path; _openMfuDumpPicker(); return; }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable"); _showMfuTagMenu(); return;
  }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) { ShowStatusAction::show("Open failed"); _showMfuTagMenu(); return; }
  if (f.size() != 540) { f.close(); ShowStatusAction::show("Invalid NTAG215 dump"); _showMfuTagMenu(); return; }
  const size_t got = f.read(_mfuDump, 540);
  f.close();
  if (got != 540) { ShowStatusAction::show("Read failed"); _showMfuTagMenu(); return; }
  _mfuDumpLen = 540; _mfuPages = 135; _mfuType = "NTAG215";
  _showMfuWritePreview(true);
}

void ST25R3916Screen::_showMfuWritePreview(bool fromFile) {
  _mfuWritePreviewFromFile = fromFile;
  if (_mfuType != "NTAG215" || _mfuPages != 135 || _mfuDumpLen != 540) {
    ShowStatusAction::show("Write supports NTAG215", 1400); _showMfuTagMenu(); return;
  }
  _rowCount = 0;
  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label; _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]}; ++_rowCount;
  };
  addRow("Source", fromFile ? "File" : "Read Tag");
  addRow("Type", "NTAG215");
  String uid;
  const uint8_t fileUid[7] = {_mfuDump[0], _mfuDump[1], _mfuDump[2], _mfuDump[4], _mfuDump[5], _mfuDump[6], _mfuDump[7]};
  for (uint8_t i = 0; i < 7; ++i) { char h[4]; snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", fileUid[i]); uid += h; }
  addRow("UID", uid);
  addRow("UID Action", "Preserved");
  addRow("Pages", "135");
  addRow("Dump", "540 bytes");
  const uint8_t* ndef = nullptr; size_t ndefLen = 0; NdefParser::Result parsed;
  if (NdefParser::extractType2Ndef(_mfuDump, _mfuDumpLen, &ndef, &ndefLen) && NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT: addRow("NDEF", "Text"); break;
      case NdefParser::RECORD_URL: addRow("NDEF", "URL"); break;
      case NdefParser::RECORD_PHONE: addRow("NDEF", "Phone"); break;
      case NdefParser::RECORD_EMAIL: addRow("NDEF", "Email"); break;
      case NdefParser::RECORD_VCARD: addRow("NDEF", "vCard"); break;
      default: addRow("NDEF", "Unsupported"); break;
    }
  } else addRow("NDEF", "Not found");
  addRow("[Press]", "Write to Tag");
  _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFU_WRITE_PREVIEW; render();
}

bool ST25R3916Screen::_writeMfuDumpToTag() {
#if defined(DEVICE_HAS_ST25R3916)
  if (_mfuDumpLen != 540 || _mfuPages != 135 || _mfuType != "NTAG215") {
    ShowStatusAction::show("Invalid NTAG215 dump"); _showMfuTagMenu(); return false;
  }
  _state = STATE_MFU_WRITING; render();
  ST25R3916Backend dev; bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (!ready) ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  if (!ready) { ShowStatusAction::show("ST25R3916 not found"); _showMfuTagMenu(); return false; }
  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) { ShowStatusAction::show("No tag detected"); _showMfuTagMenu(); return false; }
  if (tag.sak != 0x00) { dev.deactivate(); ShowStatusAction::show("Tag must be NTAG215", 1500); _showMfuTagMenu(); return false; }
  String type; uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages) || type != "NTAG215" || pages != 135) {
    dev.deactivate(); ShowStatusAction::show("Tag must be NTAG215", 1500); _showMfuTagMenu(); return false;
  }
  static constexpr uint16_t firstPage = 4, lastPage = 129, total = 126;
  ProgressView::init();
  for (uint16_t page = firstPage; page <= lastPage; ++page) {
    char msg[36]; const uint16_t done = page - firstPage;
    snprintf(msg, sizeof(msg), "Writing pages (%u/%u)...", (unsigned)(done + 1), (unsigned)total);
    ProgressView::progress(msg, (int)((uint32_t)done * 100U / total));
    if (!dev.type2WritePage((uint8_t)page, &_mfuDump[page * 4U])) {
      ProgressView::finish(); dev.deactivate(); _showMfuWritePreview(_mfuWritePreviewFromFile);
      ShowStatusAction::show("Tag write failed", 1600); render(); return false;
    }
  }
  ProgressView::progress("Write complete", 100); ProgressView::finish(); dev.deactivate();
  ShowStatusAction::show("Tag written", 1600); _showMfuTagMenu(); return true;
#else
  ShowStatusAction::show("ST25R3916 not supported"); return false;
#endif
}


void ST25R3916Screen::_eraseMfuTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFU_ERASING;
  render();

  ST25R3916Backend dev;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (!ready) ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not found");
    _showMfuTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) {
    ShowStatusAction::show("No tag detected");
    _showMfuTagMenu();
    return;
  }
  if (tag.sak != 0x00) {
    dev.deactivate();
    ShowStatusAction::show("Tag must be NTAG215", 1500);
    _showMfuTagMenu();
    return;
  }

  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages) || type != "NTAG215" || pages != 135 || tag.nfcidLen != 7) {
    dev.deactivate();
    ShowStatusAction::show("Tag must be NTAG215", 1500);
    _showMfuTagMenu();
    return;
  }

  uint8_t image[NfcDumpBuilder::NTAG215_SIZE] = {};
  size_t imageLen = 0;
  if (!NfcDumpBuilder::buildNtag215(tag.nfcid, nullptr, 0, image, imageLen, sizeof(image)) ||
      imageLen != NfcDumpBuilder::NTAG215_SIZE) {
    dev.deactivate();
    ShowStatusAction::show("Cannot build empty tag", 1500);
    _showMfuTagMenu();
    return;
  }

  static constexpr uint16_t firstPage = 4;
  static constexpr uint16_t lastPage = 129;
  static constexpr uint16_t total = 126;
  ProgressView::init();
  ProgressView::progress("Erasing pages (0/126)...", 0);

  for (uint16_t page = firstPage; page <= lastPage; ++page) {
    const uint16_t done = page - firstPage;
    char msg[40];
    snprintf(msg, sizeof(msg), "Erasing pages (%u/%u)...",
             (unsigned)(done + 1), (unsigned)total);
    ProgressView::progress(msg, (int)((uint32_t)done * 100U / total));
    if (!dev.type2WritePage((uint8_t)page, &image[page * 4U])) {
      ProgressView::finish();
      dev.deactivate();
      ShowStatusAction::show("Erase failed", 1600);
      _showMfuTagMenu();
      return;
    }
  }

  ProgressView::progress("Erase complete", 100);
  ProgressView::finish();
  dev.deactivate();

  _mfuDumpLen = 0;
  _mfuPages = 0;
  _mfuUidLen = 0;
  _mfuType = "";
  ShowStatusAction::show("Tag erased", 1600);
  _showMfuTagMenu();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_saveMfuDump() {
  if (!_mfuDumpLen || !_mfuUidLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Save failed", 1200);
    render();
    return;
  }

  String typeName = _mfuType;
  typeName.replace("MIFARE ", "MF-");
  typeName.replace("Ultralight ", "UL-");
  typeName.replace(" ", "-");
  typeName.replace("/", "-");

  String suggested = typeName + "_";
  for (uint8_t i = 0; i < _mfuUidLen; ++i) {
    char h[3];
    snprintf(h, sizeof(h), "%02X", _mfuUid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("Save dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }
  render();

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  const String filename = name + ".bin";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  const String path = String("/unigeek/nfc/dumps/") + filename;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (f) {
    ok = f.write(_mfuDump, _mfuDumpLen) == _mfuDumpLen;
    f.close();
  }

  render();
  if (ok) {
    const String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1500);
  } else {
    ShowStatusAction::show("Save failed", 1200);
  }
  render();
}

void ST25R3916Screen::_renderMfuDump() {
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  static constexpr int kRowH = 14;
  static constexpr int kScrollW = 3;
  const int fullyVisible = max(1, bh / kRowH);
  const int visible = fullyVisible + ((bh % kRowH >= 5) ? 1 : 0);
  const uint16_t totalRows = (_mfuPages + 1U) / 2U;
  const int textW = bw - kScrollW - 4;

  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  for (int i = 0; i < visible; ++i) {
    const uint16_t row = _mfuDumpOffset + (uint16_t)i;
    if (row >= totalRows) break;
    const uint16_t firstPage = row * 2U;
    const size_t off = (size_t)firstPage * 4U;
    const size_t remaining = _mfuDumpLen > off ? _mfuDumpLen - off : 0;
    const uint8_t bytes = (uint8_t)min((size_t)8, remaining);

    char label[16];
    if (firstPage + 1U < _mfuPages) snprintf(label, sizeof(label), "P%u-%u", (unsigned)firstPage, (unsigned)(firstPage + 1U));
    else snprintf(label, sizeof(label), "P%u", (unsigned)firstPage);
    char value[24] = {};
    size_t valuePos = 0;
    for (uint8_t b = 0; b < bytes; ++b) {
      valuePos += snprintf(value + valuePos, sizeof(value) - valuePos,
                           "%02X%s", _mfuDump[off + b], b + 1U == bytes ? "" : " ");
    }

    const int rowY = by + i * kRowH;
    const int rowH = min(kRowH, by + bh - rowY);
    if (rowH <= 0) break;
    Sprite sp(&lcd);
    sp.createSprite(bw - kScrollW, rowH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawString(label, 0, 2);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(value, min(42, textW / 3), 2);
    sp.pushSprite(bx, rowY);
    sp.deleteSprite();
  }

  if (totalRows > (uint16_t)fullyVisible) {
    const int trackH = bh;
    const int thumbH = max(6, (trackH * fullyVisible) / (int)totalRows);
    const int maxOff = max(1, (int)totalRows - fullyVisible);
    const int thumbY = by + ((trackH - thumbH) * min((int)_mfuDumpOffset, maxOff)) / maxOff;
    lcd.fillRect(bx + bw - kScrollW, by, kScrollW, bh, TFT_DARKGREY);
    lcd.fillRect(bx + bw - kScrollW, thumbY, kScrollW, thumbH, TFT_WHITE);
  }
}

void ST25R3916Screen::_handleMfuDumpNav(INavigation::Direction dir) {
  static constexpr int kRowH = 14;
  const uint16_t totalRows = (_mfuPages + 1U) / 2U;
  const uint16_t visible = (uint16_t)max(1, bodyH() / kRowH);
  const uint16_t maxOffset = totalRows > visible ? totalRows - visible : 0;

  if (dir == INavigation::DIR_UP) {
    if (_mfuDumpOffset > 0) --_mfuDumpOffset;
  } else if (dir == INavigation::DIR_DOWN) {
    if (_mfuDumpOffset < maxOffset) ++_mfuDumpOffset;
  } else if (dir == INavigation::DIR_LEFT) {
    _mfuDumpOffset = _mfuDumpOffset > visible ? _mfuDumpOffset - visible : 0;
  } else if (dir == INavigation::DIR_RIGHT) {
    _mfuDumpOffset = min((uint16_t)(_mfuDumpOffset + visible), maxOffset);
  }
  render();
}

void ST25R3916Screen::_readMfcNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_NDEF_READING;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;
  render();

  ST25R3916Backend dev;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (!ready) ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not found");
    _showMfcNdefMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) {
    ShowStatusAction::show("No tag detected");
    _showMfcNdefMenu();
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Not MIFARE Classic");
    _showMfcNdefMenu();
    return;
  }

  size_t totalSectors = 0, totalBlocks = 0;
  mfcDimensions(tag.sak, totalSectors, totalBlocks);
  if (!totalSectors) {
    ShowStatusAction::show("Unsupported MIFARE Classic");
    _showMfcNdefMenu();
    return;
  }

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 1200, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  static const uint8_t madKeys[][6] = {
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  };

  auto readMadBlock = [&](uint8_t block, uint8_t out[16]) -> bool {
    const uint8_t sector = block < 128 ? (uint8_t)(block / 4U)
                                       : (uint8_t)(32U + (block - 128U) / 16U);
    const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
    for (const auto& key : madKeys) {
      for (uint8_t keyType = 0; keyType < 2; ++keyType) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        if (dev.mifareClassicAuthenticate(trailer, key, keyType == 1) &&
            dev.mifareClassicReadBlock(block, out)) {
          dev.deactivate();
          return true;
        }
        dev.deactivate();
      }
    }
    return false;
  };

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  auto addIfNdef = [&](uint8_t sector, uint8_t application, uint8_t cluster) {
    if (sector >= totalSectors || sectorCount >= sizeof(sectors)) return;
    if ((application == 0x03 && cluster == 0xE1) ||
        (application == 0xE1 && cluster == 0x03)) {
      sectors[sectorCount++] = sector;
    }
  };

  uint8_t b1[16] = {}, b2[16] = {};
  if (!readMadBlock(1, b1) || !readMadBlock(2, b2)) {
    ShowStatusAction::show("No NDEF sectors in MAD");
    _showMfcNdefMenu();
    return;
  }
  for (uint8_t sec = 1; sec <= 7; ++sec) {
    const size_t off = 2U + (size_t)(sec - 1U) * 2U;
    addIfNdef(sec, b1[off], b1[off + 1U]);
  }
  for (uint8_t sec = 8; sec <= 15; ++sec) {
    const size_t off = (size_t)(sec - 8U) * 2U;
    addIfNdef(sec, b2[off], b2[off + 1U]);
  }

  if (totalSectors > 16) {
    uint8_t m0[16] = {}, m1[16] = {}, m2[16] = {};
    if (readMadBlock(64, m0) && readMadBlock(65, m1) && readMadBlock(66, m2)) {
      for (uint8_t sec = 17; sec <= 23; ++sec) {
        const size_t off = 2U + (size_t)(sec - 17U) * 2U;
        addIfNdef(sec, m0[off], m0[off + 1U]);
      }
      for (uint8_t sec = 24; sec <= 31; ++sec) {
        const size_t off = (size_t)(sec - 24U) * 2U;
        addIfNdef(sec, m1[off], m1[off + 1U]);
      }
      for (uint8_t sec = 32; sec <= 39; ++sec) {
        const size_t off = (size_t)(sec - 32U) * 2U;
        addIfNdef(sec, m2[off], m2[off + 1U]);
      }
    }
  }

  if (!sectorCount) {
    ShowStatusAction::show("No NDEF sectors in MAD");
    _showMfcNdefMenu();
    return;
  }

  _ndefCapacity = 0;
  for (size_t i = 0; i < sectorCount; ++i)
    _ndefCapacity += sectors[i] < 32 ? 48U : 240U;

  uint8_t* area = new uint8_t[_ndefCapacity];
  if (!area) {
    ShowStatusAction::show("Out of memory");
    _showMfcNdefMenu();
    return;
  }
  memset(area, 0, _ndefCapacity);

  static const uint8_t nfcKeyA[6] = {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7};
  size_t out = 0;
  size_t totalDataBlocks = 0;
  for (size_t i = 0; i < sectorCount; ++i) totalDataBlocks += sectors[i] < 32 ? 3U : 15U;
  size_t done = 0;

  ProgressView::init();
  for (size_t si = 0; si < sectorCount; ++si) {
    const uint8_t sector = sectors[si];
    const size_t first = sectorFirstBlock(sector);
    const uint8_t dataBlocks = sector < 32 ? 3 : 15;
    const uint8_t trailer = (uint8_t)(first + sectorBlockCount(sector) - 1U);

    if (!dev.hasActiveTag() && !reactivate()) {
      ProgressView::finish(); delete[] area;
      ShowStatusAction::show("Failed to read NDEF sectors"); _showMfcNdefMenu(); return;
    }
    if (!dev.mifareClassicAuthenticate(trailer, nfcKeyA, false)) {
      dev.deactivate();
      ProgressView::finish(); delete[] area;
      ShowStatusAction::show("Failed to read NDEF sectors"); _showMfcNdefMenu(); return;
    }

    for (uint8_t bi = 0; bi < dataBlocks; ++bi) {
      char msg[40];
      snprintf(msg, sizeof(msg), "Reading blocks (%u/%u)...",
               (unsigned)(done + 1U), (unsigned)totalDataBlocks);
      ProgressView::progress(msg, totalDataBlocks ? (int)(done * 100U / totalDataBlocks) : 0);
      if (!dev.mifareClassicReadBlock((uint8_t)(first + bi), area + out)) {
        dev.deactivate();
        ProgressView::finish(); delete[] area;
        ShowStatusAction::show("Failed to read NDEF sectors"); _showMfcNdefMenu(); return;
      }
      out += 16;
      ++done;
    }
    dev.deactivate();
  }
  ProgressView::finish();

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  size_t pos = 0;
  while (pos < out) {
    const uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE || pos >= out) break;
    size_t len = area[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= out) break;
      len = ((size_t)area[pos] << 8) | area[pos + 1U];
      pos += 2;
    }
    if (pos + len > out) break;
    if (tlv == 0x03) { ndef = area + pos; ndefLen = len; break; }
    pos += len;
  }

  _showNdefDetails(tag.nfcid, tag.nfcidLen, ndef, ndefLen);
  delete[] area;
#else
  ShowStatusAction::show("ST25R3916 not supported");
  _showMfcNdefMenu();
#endif
}

bool ST25R3916Screen::_detectMfuType(ST25R3916Backend& dev, String& type, uint16_t& pages) {
#if defined(DEVICE_HAS_ST25R3916)
  type = "MIFARE Ultralight / NTAG";
  pages = 0;

  // GET_VERSION identifies NTAG21x and Ultralight EV1 without relying on
  // memory-boundary guesses.
  const uint8_t getVersion = 0x60;
  uint8_t version[16] = {};
  size_t versionLen = 0;
  if (dev.type2Transceive(&getVersion, 1, version, sizeof(version), versionLen, 20) &&
      versionLen >= 8 && version[0] == 0x00 && version[1] == 0x04) {
    if (version[2] == 0x04) {
      switch (version[6]) {
        case 0x0B: type = "NTAG210"; pages = 20; return true;
        case 0x0E: type = "NTAG212"; pages = 41; return true;
        case 0x0F: type = "NTAG213"; pages = 45; return true;
        case 0x11: type = "NTAG215"; pages = 135; return true;
        case 0x13: type = "NTAG216"; pages = 231; return true;
        default: return false;
      }
    }
    if (version[2] == 0x03) {
      switch (version[6]) {
        case 0x0B: type = "Ultralight EV1 11"; pages = 20; return true;
        case 0x0E: type = "Ultralight EV1 21"; pages = 41; return true;
        default: return false;
      }
    }
    return false;
  }

  // Original MIFARE Ultralight has 16 pages and no GET_VERSION. A READ at
  // page 0 succeeds while page 16 is outside its memory.
  uint8_t data[16] = {};
  if (!dev.type2ReadPages(0, data)) return false;
  if (!dev.type2ReadPages(16, data)) {
    type = "MIFARE Ultralight";
    pages = 16;
    return true;
  }

  // Ultralight C and other proprietary/unknown Type-2 variants are left for
  // a later increment rather than guessing a writable memory boundary.
  return false;
#else
  (void)dev; (void)type; (void)pages;
  return false;
#endif
}

void ST25R3916Screen::_readMfuTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFU_READING;
  render();

  ST25R3916Backend dev;
  const char* bus = nullptr;
  bool ready = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  if (ready) bus = "I2C";
  else {
    ready = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
    if (ready) bus = "SPI";
  }
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not found");
    _showMfuTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 3000, true)) {
    ShowStatusAction::show("No tag detected", 1200);
    _showMfuTagMenu();
    return;
  }
  if (tag.sak != 0x00) {
    ShowStatusAction::show("Not Ultralight / NTAG");
    _showMfuTagMenu();
    return;
  }

  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages) || pages == 0 || pages * 4U > kMfuMaxDumpLen) {
    ShowStatusAction::show("Unsupported Type 2 tag", 1400);
    _showMfuTagMenu();
    return;
  }

  _mfuDumpLen = 0;
  _mfuPages = pages;
  _mfuType = type;
  _mfuUidLen = tag.nfcidLen;
  memcpy(_mfuUid, tag.nfcid, tag.nfcidLen);
  memcpy(_mfuAtqa, tag.atqa, sizeof(_mfuAtqa));
  _mfuSak = tag.sak;

  ProgressView::init();
  bool complete = true;
  uint16_t page = 0;
  while (page < pages) {
    // Type-2 READ always returns four consecutive pages. For a tag whose page
    // count is not a multiple of four, anchor the final READ at pages - 4 so
    // the command never asks the tag to cross its physical memory boundary.
    const uint16_t remaining = pages - page;
    const uint16_t readPage = remaining >= 4U ? page : (pages - 4U);
    const uint16_t sourceOffset = page - readPage;
    const uint16_t copyPages = remaining >= 4U ? 4U : remaining;

    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...",
             (unsigned)(page + copyPages), (unsigned)pages);
    ProgressView::progress(msg, (int)((uint32_t)page * 100U / pages));

    uint8_t fourPages[16] = {};
    if (!dev.type2ReadPages((uint8_t)readPage, fourPages)) {
      complete = false;
      break;
    }
    memcpy(&_mfuDump[page * 4U], &fourPages[sourceOffset * 4U], copyPages * 4U);
    _mfuDumpLen += copyPages * 4U;
    page += copyPages;
  }
  ProgressView::finish();
  dev.deactivate();

  if (!complete || _mfuDumpLen != (size_t)pages * 4U) {
    _mfuDumpLen = 0;
    ShowStatusAction::show("Read failed", 1200);
    _showMfuTagMenu();
    return;
  }

  _rowCount = 0;
  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    ++_rowCount;
  };

  String uid;
  for (uint8_t i = 0; i < _mfuUidLen; ++i) {
    char h[4];
    snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", _mfuUid[i]);
    uid += h;
  }
  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%02X:%02X", _mfuAtqa[0], _mfuAtqa[1]);
  char sak[4];
  snprintf(sak, sizeof(sak), "%02X", _mfuSak);

  addRow("Type", _mfuType);
  addRow("UID", uid);
  addRow("ATQA", atqa);
  addRow("SAK", sak);
  addRow("Pages", String(_mfuPages));
  addRow("Dump", String(_mfuDumpLen) + " bytes");

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;
  if (NdefParser::extractType2Ndef(_mfuDump, _mfuDumpLen, &ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT: addRow("NDEF", "Text"); break;
      case NdefParser::RECORD_URL: addRow("NDEF", "URL"); break;
      case NdefParser::RECORD_PHONE: addRow("NDEF", "Phone"); break;
      case NdefParser::RECORD_EMAIL: addRow("NDEF", "Email"); break;
      case NdefParser::RECORD_VCARD: addRow("NDEF", "vCard"); break;
      default: addRow("NDEF", "Unsupported"); break;
    }
  } else {
    addRow("NDEF", "Not found");
  }
  addRow("Reader", bus ? bus : "--");
  addRow("[Press]", "Actions");

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFU_DETAILS;
  render();
#endif
}

void ST25R3916Screen::_showI2CInfo() {
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend dev;
  bool ok = dev.beginI2C(Uni.ExI2C, ST25R3916_I2C_ADDR);
  const auto& info = dev.info();

  char msg[112];
  if (ok) {
    snprintf(msg, sizeof(msg), "I2C OK 0x%02X | ID 0x%02X | %s",
             ST25R3916_I2C_ADDR, info.chipId,
             info.is3916B ? "ST25R3916B" : (info.is3916 ? "ST25R3916" : "ST25R3916 family"));
  } else if (!info.busDetected) {
    snprintf(msg, sizeof(msg), "No device at I2C 0x%02X", ST25R3916_I2C_ADDR);
  } else {
    snprintf(msg, sizeof(msg), "I2C found; RFAL init failed (%u)", (unsigned)info.initCode);
  }
  ShowStatusAction::show(msg, 3000);
  render();
#endif
}

void ST25R3916Screen::_showSPIInfo() {
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend dev;
  bool ok = dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
  const auto& info = dev.info();

  char msg[112];
  if (ok) {
    snprintf(msg, sizeof(msg), "SPI OK | ID 0x%02X | %s", info.chipId,
             info.is3916B ? "ST25R3916B" : (info.is3916 ? "ST25R3916" : "ST25R3916 family"));
  } else {
    snprintf(msg, sizeof(msg), "SPI RFAL init failed (%u)", (unsigned)info.initCode);
  }
  ShowStatusAction::show(msg, 3000);
  render();
#endif
}
