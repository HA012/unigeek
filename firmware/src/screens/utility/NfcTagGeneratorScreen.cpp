#include "NfcTagGeneratorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/utility/NdefGeneratorScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/NfcTagBuilder.h"

#if defined(ESP32)
#include <esp_system.h>
#endif

static String _sanitizeTagName(String name) {
  name.trim();
  if (name.length() == 0) name = "ntag215";

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  if (name.endsWith(".ndef")) name.remove(name.length() - 5);

  for (int i = 0; i < (int)name.length(); i++) {
    const char c = name[i];
    const bool ok =
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_';
    if (!ok) name.setCharAt(i, '_');
  }

  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  if (name.length() == 0) name = "ntag215";
  return name;
}

const char* NfcTagGeneratorScreen::title() {
  switch (_state) {
    case STATE_TAG_TYPE:         return "Generate NFC Tag";
    case STATE_NDEF_CONTENT:     return "NDEF Content";
    case STATE_NDEF_TYPE:        return "Generate New";
    case STATE_NDEF_FILE_SELECT: return "NDEF Files";
  }
  return "Generate NFC Tag";
}

void NfcTagGeneratorScreen::onInit() {
  _goTagType();
}

void NfcTagGeneratorScreen::onBack() {
  switch (_state) {
    case STATE_TAG_TYPE:
      Screen.goBack();
      break;

    case STATE_NDEF_CONTENT:
      _goTagType();
      break;

    case STATE_NDEF_TYPE:
      _goNdefContent();
      break;

    case STATE_NDEF_FILE_SELECT:
      if (_ndefPickDir == _nfcPath || _ndefPickDir.length() == 0) {
        _ndefPickDir = "";
        _goNdefContent();
      } else {
        int slash = _ndefPickDir.lastIndexOf('/');
        String parent = (slash > 0) ? _ndefPickDir.substring(0, slash) : String(_nfcPath);
        if (!parent.startsWith(_nfcPath)) parent = _nfcPath;
        _ndefPickDir = parent;
        _openNdefFiles();
      }
      break;
  }
}

void NfcTagGeneratorScreen::onItemSelected(uint8_t index) {
  switch (_state) {
    case STATE_TAG_TYPE:
      if (index == 0) {
        _tagType = TAG_MIFARE_CLASSIC_1K;
        _goNdefContent();
      } else if (index == 1) {
        _tagType = TAG_NTAG215;
        _goNdefContent();
      }
      break;

    case STATE_NDEF_CONTENT:
      if (index == 0) {
        _goNdefType();
      } else if (index == 1) {
        _ndefPickDir = _ndefPath;
        _openNdefFiles();
      } else if (index == 2) {
        _saveTag(nullptr, 0, _tagType == TAG_MIFARE_CLASSIC_1K ? "mfc1k_empty" : "ntag215_empty");
        render();
      }
      break;

    case STATE_NDEF_TYPE: {
      uint8_t ndef[NdefGeneratorScreen::MAX_NDEF_BYTES] = {};
      size_t ndefLen = 0;
      String suggestedName;
      if (NdefGeneratorScreen::buildRecordInteractive(
              index, ndef, ndefLen, sizeof(ndef), suggestedName)) {
        _saveTag(ndef, ndefLen, suggestedName);
      }
      render();
      break;
    }

    case STATE_NDEF_FILE_SELECT:
      _selectNdefFile(index);
      break;
  }
}

void NfcTagGeneratorScreen::_goTagType() {
  _state = STATE_TAG_TYPE;
  setItems(_tagItems);
}

void NfcTagGeneratorScreen::_goNdefContent() {
  _state = STATE_NDEF_CONTENT;
  setItems(_contentItems);
}

void NfcTagGeneratorScreen::_goNdefType() {
  _state = STATE_NDEF_TYPE;
  setItems(_ndefTypeItems);
}

void NfcTagGeneratorScreen::_openNdefFiles() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    _goNdefContent();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  if (_ndefPickDir.length() == 0) _ndefPickDir = _ndefPath;
  _browser.root = _nfcPath;
  _state = STATE_NDEF_FILE_SELECT;

  uint8_t n = _browser.load(this, _ndefPickDir, ".ndef");
  if (n == 0) {
    ShowStatusAction::show("No .ndef files", 1500);
    _ndefPickDir = "";
    _goNdefContent();
    return;
  }
  setItems(_browser.items(), n);
}

void NfcTagGeneratorScreen::_selectNdefFile(uint8_t index) {
  if (index >= _browser.count()) return;

  const auto& e = _browser.entry(index);
  if (e.isDir) {
    _ndefPickDir = e.path;
    _openNdefFiles();
    return;
  }

  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Read failed", 1500);
    _openNdefFiles();
    return;
  }

  const size_t len = f.size();
  if (len == 0 || len > MAX_INPUT_NDEF) {
    f.close();
    ShowStatusAction::show(len == 0 ? "Empty .ndef file" : "NDEF file too large", 1500);
    _openNdefFiles();
    return;
  }

  uint8_t ndef[MAX_INPUT_NDEF];
  const size_t got = f.read(ndef, len);
  f.close();
  if (got != len) {
    ShowStatusAction::show("Read failed", 1500);
    _openNdefFiles();
    return;
  }

  String base = e.name;
  if (base.endsWith(".ndef")) base.remove(base.length() - 5);
  _saveTag(ndef, len, base);
  _ndefPickDir = "";
  _goNdefContent();
}


bool NfcTagGeneratorScreen::_saveTag(const uint8_t* ndef, size_t ndefLen,
                                     const String& suggestedName) {
  if (_tagType == TAG_MIFARE_CLASSIC_1K) {
    return _saveMifareClassic1K(ndef, ndefLen, suggestedName);
  }
  return _saveNtag215(ndef, ndefLen, suggestedName);
}

void NfcTagGeneratorScreen::_generateMifareUid(uint8_t uid[4]) {
#if defined(ESP32)
  const uint32_t r = esp_random();
  uid[0] = (uint8_t)(r >> 0);
  uid[1] = (uint8_t)(r >> 8);
  uid[2] = (uint8_t)(r >> 16);
  uid[3] = (uint8_t)(r >> 24);
#else
  for (uint8_t i = 0; i < 4; i++) uid[i] = (uint8_t)random(0, 256);
#endif
}

bool NfcTagGeneratorScreen::_saveMifareClassic1K(
    const uint8_t* ndef, size_t ndefLen, const String& suggestedName) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    return false;
  }

  uint8_t uid[4] = {};
  _generateMifareUid(uid);

  uint8_t image[NfcTagBuilder::MIFARE_CLASSIC_1K_SIZE] = {};
  size_t imageLen = 0;
  if (!NfcTagBuilder::buildMifareClassic1K(
          uid, ndef, ndefLen, image, imageLen, sizeof(image))) {
    ShowStatusAction::show("Cannot build MFC1K", 1500);
    return false;
  }

  String name = InputTextAction::popup("File name", suggestedName.c_str());
  if (InputTextAction::wasCancelled()) return false;

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_dumpPath);

  const String base = _sanitizeTagName(name);
  String path = String(_dumpPath) + "/" + base + ".bin";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_dumpPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const size_t written = f.write(image, imageLen);
  f.close();
  if (written != imageLen) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}

void NfcTagGeneratorScreen::_generateUid(uint8_t uid[7]) {
  uid[0] = 0x04; // NXP manufacturer ID

#if defined(ESP32)
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uid[1] = (uint8_t)(r1 >> 0);
  uid[2] = (uint8_t)(r1 >> 8);
  uid[3] = (uint8_t)(r1 >> 16);
  uid[4] = (uint8_t)(r1 >> 24);
  uid[5] = (uint8_t)(r2 >> 0);
  uid[6] = (uint8_t)(r2 >> 8);
#else
  for (uint8_t i = 1; i < 7; i++) uid[i] = (uint8_t)random(0, 256);
#endif
}

bool NfcTagGeneratorScreen::_saveNtag215(const uint8_t* ndef, size_t ndefLen,
                                         const String& suggestedName) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    return false;
  }

  uint8_t uid[7] = {};
  _generateUid(uid);

  uint8_t image[NfcTagBuilder::NTAG215_SIZE] = {};
  size_t imageLen = 0;
  if (!NfcTagBuilder::buildNtag215(uid, ndef, ndefLen,
                                   image, imageLen, sizeof(image))) {
    ShowStatusAction::show("Cannot build NTAG215", 1500);
    return false;
  }

  String name = InputTextAction::popup("File name", suggestedName.c_str());
  if (InputTextAction::wasCancelled()) return false;

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_dumpPath);

  const String base = _sanitizeTagName(name);
  String path = String(_dumpPath) + "/" + base + ".bin";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_dumpPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const size_t written = f.write(image, imageLen);
  f.close();
  if (written != imageLen) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}
