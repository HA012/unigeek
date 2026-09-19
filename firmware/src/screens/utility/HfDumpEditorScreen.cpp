#include "HfDumpEditorScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/utility/FileHexViewerScreen.h"
#include "screens/utility/NdefEditorScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputNumberAction.h"

static String sanitizeDumpName(String name) {
  name.trim();
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  if (name.endsWith(".ndef")) name.remove(name.length() - 5);

  for (int i = 0; i < (int)name.length(); ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) name.setCharAt(i, '_');
  }
  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  return name;
}

static const char* canonicalDumpTypeName(HfDumpParser::Type type) {
  switch (type) {
    case HfDumpParser::TYPE_MIFARE_CLASSIC_MINI: return "MF-Mini";
    case HfDumpParser::TYPE_MIFARE_CLASSIC_1K:   return "MF-1K";
    case HfDumpParser::TYPE_MIFARE_CLASSIC_2K:   return "MF-2K";
    case HfDumpParser::TYPE_MIFARE_CLASSIC_4K:   return "MF-4K";
    case HfDumpParser::TYPE_NTAG210: return "NTAG210";
    case HfDumpParser::TYPE_NTAG212: return "NTAG212";
    case HfDumpParser::TYPE_NTAG213: return "NTAG213";
    case HfDumpParser::TYPE_NTAG215: return "NTAG215";
    case HfDumpParser::TYPE_NTAG216: return "NTAG216";
    default: return nullptr;
  }
}

static bool parseHexBytes(const String& text, uint8_t* out, size_t count);


static uint16_t ntagConfig0(HfDumpParser::Type type) {
  switch (type) {
    case HfDumpParser::TYPE_NTAG210: return 16;
    case HfDumpParser::TYPE_NTAG212: return 37;
    case HfDumpParser::TYPE_NTAG213: return 41;
    case HfDumpParser::TYPE_NTAG215: return 131;
    case HfDumpParser::TYPE_NTAG216: return 227;
    default: return 0xFFFF;
  }
}

static uint16_t ntagDynamicLockPage(HfDumpParser::Type type) {
  switch (type) {
    case HfDumpParser::TYPE_NTAG212: return 36;
    case HfDumpParser::TYPE_NTAG213: return 40;
    case HfDumpParser::TYPE_NTAG215: return 130;
    case HfDumpParser::TYPE_NTAG216: return 226;
    default: return 0xFFFF;
  }
}

static const char* ntagSensitivePageLabel(HfDumpParser::Type type, uint16_t page) {
  const uint16_t dynamicLock = ntagDynamicLockPage(type);
  const uint16_t config0 = ntagConfig0(type);
  if (page == dynamicLock) return "Dynamic lock page";
  if (page == config0 || page == config0 + 1) return "Configuration page";
  if (page == config0 + 2) return "Password page";
  if (page == config0 + 3) return "PACK page";
  return nullptr;
}

static bool confirmNtagSensitiveWrite(HfDumpParser::Type type, uint16_t page) {
  const char* label = ntagSensitivePageLabel(type, page);
  if (!label) return true;
  static const InputSelectAction::Option opts[] = {
    {"Write anyway", "write"},
  };
  const String title = String("Warning: ") + label;
  const char* choice = InputSelectAction::popup(title.c_str(), opts, 1, nullptr);
  return choice && strcmp(choice, "write") == 0;
}

HfDumpEditorScreen::HfDumpEditorScreen(uint8_t* initialDump, size_t initialDumpLen)
    : _postCreate(true), _newUnsaved(true) {
  if (!initialDump) return;
  if (initialDumpLen == 0 || initialDumpLen > MAX_DUMP_BYTES) {
    delete[] initialDump;
    return;
  }
  _dump = initialDump;
  _dumpLen = initialDumpLen;
}

const char* HfDumpEditorScreen::title() {
  switch (_state) {
    case STATE_DUMP_INFO: return "HF Dump Details";
    case STATE_ACTIONS:   return "HF Dump Actions";
    default:              return "Edit HF Dump";
  }
}

HfDumpEditorScreen::~HfDumpEditorScreen() { _freeDump(); }

void HfDumpEditorScreen::onInit() {
  _pickDir = _dumpPath;
  if (_newUnsaved) {
    if (!_dump || _dumpLen == 0) {
      ShowStatusAction::show("Out of memory", 1600);
      Screen.goBack();
      return;
    }
    _info = HfDumpParser::inspect(_dump, _dumpLen);
    _dirty = true;
    _showInfo();
    return;
  }
  if (_initialPath.length()) {
    const String path = _initialPath;
    _initialPath = "";
    if (_loadFile(path)) {
      _filePath = path;
      _showInfo();
      return;
    }
    ShowStatusAction::show("Cannot open created dump", 1600);
  }
  _openFiles();
}

void HfDumpEditorScreen::onUpdate() {
  if (_returnToInfoAfterChild) {
    _returnToInfoAfterChild = false;
    _showInfo();
    return;
  }
  if (_state == STATE_DUMP_INFO) {
    if (!_holdFired && Uni.Nav->isPressed() &&
        Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
        Uni.Nav->heldDuration() >= 700) {
      _holdFired = true;
      Uni.Nav->suppressCurrentPress();
      if (_newUnsaved) _showActions();
      else if (_dirty) _saveAs();
      return;
    }
    if (!Uni.Nav->isPressed()) _holdFired = false;

    if (Uni.Nav->wasPressed()) {
      const auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        onBack();
        return;
      }
      if (dir == INavigation::DIR_PRESS) {
        if (_newUnsaved) {
          if (_saveAs()) Screen.goBack();
        } else if (_dirty) {
          _save();
        } else {
          _showActions();
        }
        return;
      }
      _infoView.onNav(dir);
    }
    return;
  }
  ListScreen::onUpdate();
}

void HfDumpEditorScreen::onRender() {
  if (_state == STATE_DUMP_INFO) {
    _infoView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void HfDumpEditorScreen::onBack() {
  if (_state == STATE_ACTIONS) {
    _showInfo();
    return;
  }
  if (_state == STATE_DUMP_INFO) {
    _freeDump();
    _filePath = "";
    if (_postCreate) {
      Screen.goBack();
    } else {
      _openFiles();
    }
    return;
  }
  if (_pickDir != _dumpPath) {
    int slash = _pickDir.lastIndexOf('/');
    _pickDir = (slash > (int)strlen(_dumpPath)) ? _pickDir.substring(0, slash) : String(_dumpPath);
    _openFiles();
    return;
  }
  Screen.goBack();
}

void HfDumpEditorScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_FILE_SELECT) {
    _selectFile(index);
    return;
  }

  if (_state == STATE_DUMP_INFO) {
    _showActions();
    return;
  }

  if (index >= _actionCount) return;
  switch (_actionCodes[index]) {
    case 0: {
      if (_dump) {
        const String displayPath = _filePath.length() ? _filePath : (_suggestedDumpName() + ".bin");
        Screen.push(new FileHexViewerScreen(_dump, _dumpLen, displayPath));
      }
      break;
    }
    case 1: _editMemory(); break;
    case 2: _editUid(); break;
    case 3: _editNdef(); break;
    case 4: _showPasswordInfo(); break;
    case 5: _setPassword(); break;
    case 6: _removePassword(); break;
  }
}

void HfDumpEditorScreen::_openFiles() {
  _state = STATE_FILE_SELECT;
  _browser.root = _dumpPath;
  const uint8_t count = _browser.load(this, _pickDir, ".bin", "DUMP", BrowseFileView::STEM);
  setItems(_browser.items(), count);
  render();
}

void HfDumpEditorScreen::_selectFile(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto& entry = _browser.entry(index);
  if (entry.isDir) {
    _pickDir = entry.path;
    _openFiles();
    return;
  }

  if (!_loadFile(entry.path)) {
    ShowStatusAction::show("Dump not supported", 1600);
    return;
  }

  _filePath = entry.path;
  _showInfo();
}

bool HfDumpEditorScreen::_loadFile(const String& path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "r");
  if (!f) return false;
  const size_t len = f.size();
  if (len == 0 || len > MAX_DUMP_BYTES) { f.close(); return false; }
  uint8_t* next = new uint8_t[len];
  if (!next) { f.close(); return false; }
  const size_t got = f.read(next, len);
  f.close();
  if (got != len) { delete[] next; return false; }
  const HfDumpParser::Info nextInfo = HfDumpParser::inspect(next, len);
  if (nextInfo.type == HfDumpParser::TYPE_UNKNOWN) { delete[] next; return false; }
  _freeDump();
  _dump = next;
  _dumpLen = len;
  _info = nextInfo;
  _dirty = false;
  return true;
}

void HfDumpEditorScreen::_freeDump() {
  delete[] _dump;
  _dump = nullptr;
  _dumpLen = 0;
  _dirty = false;
}


void HfDumpEditorScreen::_editMemory() {
  if (!_dump || _dumpLen == 0) return;

  if (HfDumpParser::isMifareClassic(_info.type)) {
    const size_t blocks = _dumpLen / 16u;
    if (blocks <= 1 || blocks > 256) { _showActions(); ShowStatusAction::show("Invalid Classic dump", 1600); return; }
    const int block = InputNumberAction::popup(
        (String("Block (1..") + String((unsigned int)(blocks - 1)) + ")").c_str(),
        1, (int)blocks - 1, 1);
    if (InputNumberAction::wasCancelled()) return;
    String hex = InputTextAction::popup("Block data (32 hex)", "", InputTextAction::INPUT_HEX);
    if (InputTextAction::wasCancelled()) return;
    hex.replace(" ", ""); hex.replace(":", "");
    uint8_t data[16] = {};
    if (!parseHexBytes(hex, data, 16)) {
      _showActions(); ShowStatusAction::show(hex.length() == 32 ? "Bad hex" : "Need 32 hex chars", 1600); return;
    }
    memcpy(_dump + (size_t)block * 16u, data, sizeof(data));
    _dirty = true; _info = HfDumpParser::inspect(_dump, _dumpLen);
    _showInfo(); ShowStatusAction::show("Block updated", 1600); return;
  }

  if (HfDumpParser::isType2(_info.type)) {
    const size_t pages = _dumpLen / 4u;
    if (pages <= 4 || pages > 256) { _showActions(); ShowStatusAction::show("Invalid NTAG dump", 1600); return; }
    const int page = InputNumberAction::popup(
        (String("Page (4..") + String((unsigned int)(pages - 1)) + ")").c_str(),
        4, (int)pages - 1, 4);
    if (InputNumberAction::wasCancelled()) return;
    if (!confirmNtagSensitiveWrite(_info.type, (uint16_t)page)) return;
    String hex = InputTextAction::popup("Page data (8 hex)", "", InputTextAction::INPUT_HEX);
    if (InputTextAction::wasCancelled()) return;
    hex.replace(" ", ""); hex.replace(":", "");
    uint8_t data[4] = {};
    if (!parseHexBytes(hex, data, 4)) {
      _showActions(); ShowStatusAction::show(hex.length() == 8 ? "Bad hex" : "Need 8 hex chars", 1600); return;
    }
    memcpy(_dump + (size_t)page * 4u, data, sizeof(data));
    _dirty = true; _info = HfDumpParser::inspect(_dump, _dumpLen);
    _showInfo(); ShowStatusAction::show("Page updated", 1600); return;
  }

  _showActions(); ShowStatusAction::show("Edit Memory not supported", 1600);
}


void HfDumpEditorScreen::_editUid() {
  if (!_dump || _info.uidLen == 0) return;
  if (!_info.uidValid) { _showActions(); ShowStatusAction::show("Invalid current UID/BCC", 1600); return; }

  String current;
  for (uint8_t i = 0; i < _info.uidLen; ++i) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", _info.uid[i]);
    current += hex;
  }

  String text = InputTextAction::popup("UID (hex)", current.c_str(), InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) return;

  uint8_t uid[10] = {};
  if (!parseHexBytes(text, uid, _info.uidLen)) {
    _showActions(); ShowStatusAction::show((String("UID must be ") + String(_info.uidLen * 2) + " hex digits").c_str(), 1600);
    return;
  }

  if (!HfDumpParser::setUid(_dump, _dumpLen, uid, _info.uidLen)) {
    _showActions(); ShowStatusAction::show("UID edit not supported", 1600);
    return;
  }

  _dirty = true;
  _info = HfDumpParser::inspect(_dump, _dumpLen);
  _showInfo();
  ShowStatusAction::show("UID updated", 1600);
}

bool HfDumpEditorScreen::_applyEditedNdef(void* context, const uint8_t* ndef, size_t len) {
  auto* self = static_cast<HfDumpEditorScreen*>(context);
  if (!self || !self->_dump || !ndef || len == 0) return false;
  if (!HfDumpParser::replaceNdef(self->_dump, self->_dumpLen, ndef, len)) return false;
  self->_dirty = true;
  self->_info = HfDumpParser::inspect(self->_dump, self->_dumpLen);
  self->_returnToInfoAfterChild = true;
  return true;
}

void HfDumpEditorScreen::_editNdef() {
  if (!_dump) return;
  uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  if (!HfDumpParser::extractNdef(_dump, _dumpLen, &ndef, &ndefLen)) {
    _showActions(); ShowStatusAction::show("No NDEF record", 1600);
    return;
  }
  if (ndefLen == 0) {
    delete[] ndef;
    _showActions(); ShowStatusAction::show("Empty NDEF record", 1600);
    return;
  }
  if (ndefLen > NdefEditorScreen::maxEditableNdefBytes()) {
    delete[] ndef;
    _showActions(); ShowStatusAction::show("NDEF too large for editor", 1600);
    return;
  }

  NdefEditorScreen* editor = new NdefEditorScreen(ndef, ndefLen, _applyEditedNdef, this);
  delete[] ndef;
  Screen.push(editor);
}

void HfDumpEditorScreen::_showPasswordInfo() {
  if (!_dump) return;
  const auto p = HfDumpParser::inspectPassword(_dump, _dumpLen);
  if (!p.supported) { _showActions(); ShowStatusAction::show("Password not supported", 1600); return; }
  char msg[160];
  snprintf(msg, sizeof(msg),
           "Protection: %s\nAUTH0: %02X\nAccess: %s\nAUTHLIM: %u\nConfig: %s\nPWD: %02X %02X %02X %02X\nPACK: %02X %02X",
           p.enabled ? "Enabled" : "Disabled", p.auth0,
           p.protectRead ? "Read & Write" : "Write Only", p.authLimit,
           p.configLocked ? "Locked" : "Open",
           p.pwd[0], p.pwd[1], p.pwd[2], p.pwd[3], p.pack[0], p.pack[1]);
  ShowStatusAction::show(msg, 4500);
}

static bool parseHexBytes(const String& text, uint8_t* out, size_t count) {
  if (text.length() != count * 2u) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < count; ++i) {
    const int hi = nibble(text[i * 2]);
    const int lo = nibble(text[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void HfDumpEditorScreen::_setPassword() {
  if (!_dump) return;
  const auto current = HfDumpParser::inspectPassword(_dump, _dumpLen);
  if (!current.supported) { _showActions(); ShowStatusAction::show("Password not supported", 1600); return; }
  if (current.configLocked) { _showActions(); ShowStatusAction::show("Configuration locked", 1600); return; }

  String pwdText = InputTextAction::popup("PWD (8 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) return;
  uint8_t pwd[4];
  if (!parseHexBytes(pwdText, pwd, 4)) { _showActions(); ShowStatusAction::show("PWD must be 8 hex digits", 1600); return; }

  String packText = InputTextAction::popup("PACK (4 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) return;
  uint8_t pack[2];
  if (!parseHexBytes(packText, pack, 2)) { _showActions(); ShowStatusAction::show("PACK must be 4 hex digits", 1600); return; }

  static const InputSelectAction::Option modes[] = {{"Write Only", "w"}, {"Read & Write", "rw"}};
  const char* mode = InputSelectAction::popup("Protection", modes, 2, nullptr);
  if (!mode) return;
  const bool protectRead = strcmp(mode, "rw") == 0;

  if (!HfDumpParser::setPassword(_dump, _dumpLen, pwd, pack, protectRead)) {
    _showActions(); ShowStatusAction::show("Failed", 1600);
    return;
  }
  _dirty = true;
  _info = HfDumpParser::inspect(_dump, _dumpLen);
  _showInfo(); ShowStatusAction::show("Password set", 1600);
}

void HfDumpEditorScreen::_removePassword() {
  if (!_dump) return;
  const auto current = HfDumpParser::inspectPassword(_dump, _dumpLen);
  if (!current.supported) { _showActions(); ShowStatusAction::show("Password not supported", 1600); return; }
  if (current.configLocked) { _showActions(); ShowStatusAction::show("Configuration locked", 1600); return; }
  if (!current.enabled) { _showActions(); ShowStatusAction::show("Password not set", 1600); return; }

  if (!HfDumpParser::removePassword(_dump, _dumpLen)) {
    _showActions(); ShowStatusAction::show("Failed", 1600);
    return;
  }
  _dirty = true;
  _info = HfDumpParser::inspect(_dump, _dumpLen);
  _showInfo(); ShowStatusAction::show("Password removed", 1600);
}

String HfDumpEditorScreen::_suggestedDumpName() const {
  const char* typeName = canonicalDumpTypeName(_info.type);
  if (typeName && _info.uidLen > 0) {
    String name(typeName);
    name += '_';
    for (uint8_t i = 0; i < _info.uidLen; ++i) {
      char hex[3];
      snprintf(hex, sizeof(hex), "%02X", _info.uid[i]);
      name += hex;
    }
    return name;
  }

  int slash = _filePath.lastIndexOf('/');
  String name = (slash >= 0) ? _filePath.substring(slash + 1) : _filePath;
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  return name.length() ? name : String("dump");
}

bool HfDumpEditorScreen::_writeFile(const String& path) {
  if (!_dump || _dumpLen == 0 || !Uni.Storage || !Uni.Storage->isAvailable()) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) return false;
  const size_t written = f.write(_dump, _dumpLen);
  f.close();
  return written == _dumpLen;
}

bool HfDumpEditorScreen::_save() {
  if (_newUnsaved || !_filePath.length()) return _saveAs();
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1600);
    return false;
  }
  if (!_writeFile(_filePath)) {
    ShowStatusAction::show("Failed", 1600);
    return false;
  }
  _dirty = false;
  const int slash = _filePath.lastIndexOf('/');
  const String saved = (slash >= 0) ? _filePath.substring(slash + 1) : _filePath;
  _showInfo();
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}

bool HfDumpEditorScreen::_saveAs() {
  if (!_dump || _dumpLen == 0) return false;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1600);
    return false;
  }

  String name = InputTextAction::popup("File name", _suggestedDumpName().c_str());
  if (InputTextAction::wasCancelled()) { render(); return false; }
  render();
  const String base = sanitizeDumpName(name);
  if (base.length() == 0) {
    ShowStatusAction::show("Invalid file name", 1600);
    return false;
  }

  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir(_dumpPath);

  String path = String(_dumpPath) + "/" + base + ".bin";
  if (Uni.Storage->exists(path.c_str())) {
    bool found = false;
    for (int n = 2; n < 1000; ++n) {
      const String candidate = String(_dumpPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        found = true;
        break;
      }
    }
    if (!found) {
      ShowStatusAction::show("Failed", 1600);
      return false;
    }
  }

  if (!_writeFile(path)) {
    Uni.Storage->deleteFile(path.c_str());
    ShowStatusAction::show("Failed", 1600);
    return false;
  }

  _filePath = path;
  _dirty = false;
  if (!_newUnsaved) _showInfo();
  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}

String HfDumpEditorScreen::_uidString() const {
  if (_info.uidLen == 0) return "Unknown";
  String out;
  for (uint8_t i = 0; i < _info.uidLen; ++i) {
    if (i) out += ':';
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", _info.uid[i]);
    out += hex;
  }
  if (!_info.uidValid) out += " (invalid BCC)";
  return out;
}

void HfDumpEditorScreen::_showInfo() {
  _state = STATE_DUMP_INFO;
  _infoRowCount = 0;

  auto addInfo = [&](const String& label, const String& value) {
    if (_infoRowCount >= INFO_ROW_MAX) return;
    _infoLabels[_infoRowCount] = label;
    _infoValues[_infoRowCount] = value;
    _infoRows[_infoRowCount] = {_infoLabels[_infoRowCount].c_str(), _infoValues[_infoRowCount]};
    ++_infoRowCount;
  };

  addInfo("Type", HfDumpParser::typeName(_info.type));
  addInfo("UID", _uidString());
  addInfo("Size", String((unsigned int)_info.size) + " bytes");
  addInfo("NDEF", _info.hasNdef ? "Yes" : "No");
  addInfo("NDEF Size", _info.hasNdef ? (String((unsigned int)_info.ndefLen) + " bytes") : "-");

  // Read-only preview: fields are informational, never selectable.
  if (_newUnsaved) {
    addInfo("[Press]", "Save");
    addInfo("[Hold]", "Edit");
  } else if (_dirty) {
    addInfo("[Press]", "Save");
    addInfo("[Hold]", "Save As");
  } else {
    addInfo("[Press]", "Edit");
  }

  _infoView.resetScroll();
  _infoView.setRows(_infoRows, _infoRowCount);
  render();
}

void HfDumpEditorScreen::_showActions() {
  _state = STATE_ACTIONS;
  _actionCount = 0;
  auto addAction = [&](const char* label, uint8_t code) {
    if (_actionCount >= 8) return;
    _actionItems[_actionCount] = {label};
    _actionCodes[_actionCount] = code;
    ++_actionCount;
  };

  addAction("View Memory", 0);
  addAction("Edit Memory", 1);
  addAction("Edit UID", 2);
  addAction("Edit NDEF", 3);
  if (HfDumpParser::isType2(_info.type)) {
    addAction("Password Info", 4);
    addAction("Set Password", 5);
    addAction("Remove Password", 6);
  }
  setItems(_actionItems, _actionCount);
  render();
}
