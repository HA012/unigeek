#include "ST25R3916Screen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

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
  if (_state == STATE_MENU || _state == STATE_MFC_MENU || _state == STATE_MFC_TAG_MENU) {
    ListScreen::onUpdate();
    return;
  }

  if (!Uni.Nav->wasPressed()) return;

  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    if (_state == STATE_MFC_DETAILS) _showMfcTagMenu();
    else _showMenu();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_DETAILS) {
    _scan(_lastTechMask);
    return;
  }
  _scrollView.onNav(dir);
}

void ST25R3916Screen::onRender() {
  if (_state == STATE_MENU || _state == STATE_MFC_MENU || _state == STATE_MFC_TAG_MENU) {
    ListScreen::onRender();
    return;
  }
  if (_state == STATE_SCANNING || _state == STATE_MFC_READING) {
    _renderTagPrompt();
    return;
  }
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}

void ST25R3916Screen::onBack() {
  if (_state == STATE_DETAILS) {
    _showMenu();
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
  if (_state == STATE_MFC_MENU) {
    if (index == 0) _showMfcTagMenu();
    return;
  }
  if (_state == STATE_MFC_TAG_MENU) {
    if (index == 0) _readMfcTag();
    return;
  }

  switch (index) {
    case 0: _scan(ST25R3916Backend::TECH_ALL); break;
    case 1: _showMfcMenu(); break;
    case 2: _scan(ST25R3916Backend::TECH_A); break;
    case 3: _scan(ST25R3916Backend::TECH_B); break;
    case 4: _scan(ST25R3916Backend::TECH_F); break;
    case 5: _scan(ST25R3916Backend::TECH_V); break;
    case 6: _showI2CInfo(); break;
    case 7: _showSPIInfo(); break;
  }
#else
  (void)index;
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_showMenu() {
  _state = STATE_MENU;
  setItems(_items, 8);
}

void ST25R3916Screen::_showMfcMenu() {
  _state = STATE_MFC_MENU;
  setItems(_mfcItems, 1);
  render();
}

void ST25R3916Screen::_showMfcTagMenu() {
  _state = STATE_MFC_TAG_MENU;
  setItems(_mfcTagItems, 1);
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

  const auto defaults = NFCUtility::getDefaultKeys();
  bool blockSeen[256] = {};
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
        for (size_t offset = 0; offset < count; ++offset) {
          const size_t block = first + offset;
          if (blockSeen[block]) continue;
          uint8_t data[16];
          if (!dev.mifareClassicReadBlock((uint8_t)block, data)) break;
          blockSeen[block] = true;
          ++sectorBlocksRead;
          ++blocksRead;
        }
        dev.deactivate();
        if (sectorBlocksRead == count) break;
      }
    }
    if (sectorHasKey) ++sectorsWithKey;
  }
  ProgressView::finish();

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
  addRow("Size", String((unsigned)(blocks * 16U)) + " bytes");
  addRow("Sectors", String((unsigned)sectors));
  addRow("Blocks", String((unsigned)blocksRead) + "/" + String((unsigned)blocks));
  addRow("Keys", String((unsigned)sectorsWithKey) + "/" + String((unsigned)sectors) + " sectors");
  addRow("Status", blocksRead == blocks ? "Complete" : "Partial");
  addRow("Reader", bus ? bus : "--");

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFC_DETAILS;
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
