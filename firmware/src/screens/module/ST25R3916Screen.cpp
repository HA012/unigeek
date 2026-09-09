#include "ST25R3916Screen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

#if defined(DEVICE_HAS_ST25R3916)
#include "utils/nfc/ST25R3916Backend.h"
#endif

void ST25R3916Screen::onInit() {
  setItems(_items, 7);
}

void ST25R3916Screen::onBack() {
  Screen.goBack();
}

void ST25R3916Screen::onItemSelected(uint8_t index) {
#if defined(DEVICE_HAS_ST25R3916)
  switch (index) {
    case 0: _scan(ST25R3916Backend::TECH_ALL); break;
    case 1: _scan(ST25R3916Backend::TECH_A); break;
    case 2: _scan(ST25R3916Backend::TECH_B); break;
    case 3: _scan(ST25R3916Backend::TECH_F); break;
    case 4: _scan(ST25R3916Backend::TECH_V); break;
    case 5: _showI2CInfo(); break;
    case 6: _showSPIInfo(); break;
  }
#else
  (void)index;
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_scan(uint16_t techMask) {
#if defined(DEVICE_HAS_ST25R3916)
  ShowStatusAction::show("Scanning...", 0);

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
    render();
    return;
  }

  ST25R3916Backend::ScanResult result;
  if (!dev.scan(techMask, result, 1800)) {
    ShowStatusAction::show("No tag found");
    render();
    return;
  }

  const char* tech = "Unknown";
  switch (result.technology) {
    case ST25R3916Backend::Technology::NFC_A: tech = "NFC-A"; break;
    case ST25R3916Backend::Technology::NFC_B: tech = "NFC-B"; break;
    case ST25R3916Backend::Technology::NFC_F: tech = "NFC-F"; break;
    case ST25R3916Backend::Technology::NFC_V: tech = "NFC-V"; break;
    default: break;
  }

  char uid[31] = {};
  size_t pos = 0;
  for (uint8_t i = 0; i < result.nfcidLen && pos + 3 < sizeof(uid); i++) {
    int n = snprintf(uid + pos, sizeof(uid) - pos, i ? " %02X" : "%02X", result.nfcid[i]);
    if (n <= 0) break;
    pos += (size_t)n;
  }

  char msg[160];
  if (result.technology == ST25R3916Backend::Technology::NFC_A) {
    snprintf(msg, sizeof(msg), "%s via %s | UID %s | ATQA %02X %02X | SAK %02X%s",
             tech, bus, uid[0] ? uid : "--", result.atqa[0], result.atqa[1], result.sak,
             result.isoDep ? " | ISO-DEP" : "");
  } else {
    snprintf(msg, sizeof(msg), "%s via %s | NFCID %s%s", tech, bus, uid[0] ? uid : "--",
             result.isoDep ? " | ISO-DEP" : "");
  }
  ShowStatusAction::show(msg);
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
