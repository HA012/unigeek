#include "LfDataEditorScreen.h"
#include <ctype.h>
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

static String sanitizeLfName(String name) {
  name.trim();
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  for (int i = 0; i < (int)name.length(); ++i) {
    const char c = name[i];
    const bool ok = isalnum((unsigned char)c) || c == '-' || c == '_';
    if (!ok) name.setCharAt(i, '_');
  }
  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  return name;
}

const char* LfDataEditorScreen::title() {
  switch (_state) {
    case STATE_DETAILS: return "LF Data Details";
    case STATE_ACTIONS: return "LF Data Actions";
    default: return "Edit LF Data";
  }
}

void LfDataEditorScreen::onInit() {
  _browser.root = kPath;
  _pickDir = kPath;
  if (_loaded) _showDetails(); else _openFiles();
}

void LfDataEditorScreen::onUpdate() {
  if (_state == STATE_DETAILS) {
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
      if (dir == INavigation::DIR_BACK) { onBack(); return; }
      if (dir == INavigation::DIR_PRESS) {
        if (_newUnsaved) {
          if (_saveAs()) Screen.goBack();
        } else if (_dirty) _save();
        else _showActions();
        return;
      }
      _details.onNav(dir);
    }
    return;
  }
  ListScreen::onUpdate();
}

void LfDataEditorScreen::onRender() {
  if (_state == STATE_DETAILS) { _details.render(bodyX(), bodyY(), bodyW(), bodyH()); return; }
  ListScreen::onRender();
}

void LfDataEditorScreen::onBack() {
  if (_state == STATE_ACTIONS) { _showDetails(); return; }
  if (_state == STATE_DETAILS) {
    if (_newUnsaved) Screen.goBack(); else { _loaded = false; _filePath = ""; _openFiles(); }
    return;
  }
  if (_pickDir != kPath) {
    int slash = _pickDir.lastIndexOf('/');
    _pickDir = (slash > (int)strlen(kPath)) ? _pickDir.substring(0, slash) : String(kPath);
    _openFiles();
    return;
  }
  Screen.goBack();
}

void LfDataEditorScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_FILE_SELECT) { _selectFile(index); return; }
  if (_state != STATE_ACTIONS || index >= _actionCount) return;
  const size_t editable = LFCodec::editableFieldCount(_data.protocol);
  if (index < editable) { _editField(index); return; }
}

void LfDataEditorScreen::_openFiles() {
  _state = STATE_FILE_SELECT;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { ShowStatusAction::show("Storage unavailable", 1500); Screen.goBack(); return; }
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir(kPath);
  const uint8_t n = _browser.load(this, _pickDir, ".bin", nullptr, BrowseFileView::NAME, nullptr,
                                  _pickDir == kPath ? "dictionaries" : nullptr);
  setItems(_browser.items(), n);
  render();
}

void LfDataEditorScreen::_selectFile(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto e = _browser.entry(index);
  if (e.isDir) { _pickDir = e.path; _openFiles(); return; }
  if (!_loadFile(e.path)) { ShowStatusAction::show("Data not supported", 1500); _openFiles(); }
}

bool LfDataEditorScreen::_loadFile(const String& path) {
  const auto* info = LFCodec::fromFilename(path);
  if (!info || !Uni.Storage) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "r");
  if (!f || f.size() != info->dataSize) { if (f) f.close(); return false; }
  uint8_t raw[LFCodec::kMaxDataSize] = {};
  const size_t got = f.read(raw, info->dataSize); f.close();
  if (got != info->dataSize || !LFCodec::decode(info->protocol, raw, got, _data)) return false;
  _filePath = path; _loaded = true; _newUnsaved = false; _dirty = false; _showDetails(); return true;
}

void LfDataEditorScreen::_showDetails() {
  _state = STATE_DETAILS; _rowCount = 0;
  auto add = [&](const String& l, const String& v) {
    if (_rowCount >= kMaxRows) return;
    _labels[_rowCount] = l; _values[_rowCount] = v;
    _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount]}; ++_rowCount;
  };
  const auto* info = LFCodec::format(_data.protocol);
  add("Type", info ? info->name : "Unknown");
  LFCodec::Field fields[6];
  const size_t n = LFCodec::fields(_data, fields, 6);
  for (size_t i = 0; i < n; ++i) add(fields[i].label, fields[i].value);
  if (_newUnsaved) { add("[Press]", "Save"); add("[Hold]", "Edit"); }
  else if (_dirty) { add("[Press]", "Save"); add("[Hold]", "Save As"); }
  else add("[Press]", "Edit");
  _details.resetScroll(); _details.setRows(_rows, _rowCount); render();
}

void LfDataEditorScreen::_showActions() {
  _state = STATE_ACTIONS; _actionCount = 0;
  const size_t n = LFCodec::editableFieldCount(_data.protocol);
  for (size_t i = 0; i < n && _actionCount < 7; ++i) {
    const auto* f = LFCodec::editableFieldAt(_data.protocol, i);
    if (f) _actions[_actionCount++] = {f->label};
  }
  setItems(_actions, _actionCount); render();
}

String LfDataEditorScreen::_fieldValue(const LFCodec::FieldInfo& field) const {
  switch (field.id) {
    case LFCodec::FieldId::NumericId: return LFCodec::hex(_data.data, _data.length, false);
    case LFCodec::FieldId::FacilityCode: return String(_data.facilityCode);
    case LFCodec::FieldId::CardNumber: return String(_data.cardNumber);
    case LFCodec::FieldId::TextId: return _data.hasTextId ? String(_data.textId) : String();
    case LFCodec::FieldId::RawData: return LFCodec::hex(_data.data, _data.length, false);
  }
  return "";
}

void LfDataEditorScreen::_editField(size_t index) {
  const auto* field = LFCodec::editableFieldAt(_data.protocol, index);
  if (!field) return;
  const String current = _fieldValue(*field);
  const auto mode = field->type == LFCodec::FieldType::Hex
      ? InputTextAction::INPUT_HEX
      : InputTextAction::INPUT_TEXT;
  String value = InputTextAction::popup(field->label, current.c_str(), mode);
  if (InputTextAction::wasCancelled()) { _showActions(); return; }
  if (!LFCodec::setField(_data, field->id, value)) { ShowStatusAction::show("Invalid value", 1500); _showActions(); return; }
  _dirty = true;
  _showDetails();
}

String LfDataEditorScreen::_suggestedName() const {
  const auto* info = LFCodec::format(_data.protocol);
  if (!info) return "LF_Data";
  String suffix = LFCodec::hex(_data.data, _data.length, false);
  return String(info->filePrefix) + "_" + suffix;
}

bool LfDataEditorScreen::_writeFile(const String& path) {
  uint8_t raw[LFCodec::kMaxDataSize] = {};
  if (!LFCodec::encode(_data, raw, sizeof(raw))) return false;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) return false;
  const size_t written = f.write(raw, _data.length);
  f.close();
  if (written != _data.length) return false;
  memcpy(_data.data, raw, _data.length);
  return true;
}

bool LfDataEditorScreen::_save() {
  if (_newUnsaved || !_filePath.length()) return _saveAs();
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { ShowStatusAction::show("Storage unavailable", 1500); return false; }
  if (!_writeFile(_filePath)) { ShowStatusAction::show("Failed", 1500); return false; }
  _dirty = false;
  const int slash = _filePath.lastIndexOf('/');
  _showDetails();
  ShowStatusAction::show(("Saved: " + _filePath.substring(slash + 1)).c_str(), 1500);
  return true;
}

bool LfDataEditorScreen::_saveAs() {
  uint8_t raw[LFCodec::kMaxDataSize] = {};
  if (!LFCodec::encode(_data, raw, sizeof(raw))) { ShowStatusAction::show("Invalid LF data", 1500); return false; }
  String name = InputTextAction::popup("File name", _suggestedName().c_str());
  if (InputTextAction::wasCancelled()) { render(); return false; }
  String base = sanitizeLfName(name);
  const auto* info = LFCodec::format(_data.protocol);
  if (!info || !base.length()) { ShowStatusAction::show("Invalid file name", 1500); return false; }
  const String required = String(info->filePrefix) + "_";
  if (!base.startsWith(required)) base = required + base;
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir(kPath);
  String path = String(kPath) + "/" + base + ".bin";
  if (Uni.Storage->exists(path.c_str())) {
    bool found = false;
    for (int n = 2; n < 1000; ++n) {
      String candidate = String(kPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) { path = candidate; found = true; break; }
    }
    if (!found) { ShowStatusAction::show("Failed", 1500); return false; }
  }
  if (!_writeFile(path)) { Uni.Storage->deleteFile(path.c_str()); ShowStatusAction::show("Failed", 1500); return false; }
  _filePath = path; _dirty = false;
  if (!_newUnsaved) _showDetails();
  const int slash = path.lastIndexOf('/');
  ShowStatusAction::show(("Saved: " + path.substring(slash + 1)).c_str(), 1500);
  return true;
}
