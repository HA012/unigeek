#include "ChameleonMfcKeysScreen.h"
#include "ChameleonMfcScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/MfcKeyStore.h"

const char* ChameleonMfcKeysScreen::title() {
  if (_state == STATE_DATABASES) return "Dictionaries";
  if (_state == STATE_VIEW && _viewTitle.length()) return _viewTitle.c_str();
  return "Keys";
}

void ChameleonMfcKeysScreen::onInit() {
  _goMenu();
}

// ── states ─────────────────────────────────────────────

void ChameleonMfcKeysScreen::_goMenu() {
  _state   = STATE_MENU;
  _menu[0] = {"Check Known Keys"};
  _menu[1] = {"Dictionaries"};
  setItems(_menu, 2, _selMenu);
  render();
}

void ChameleonMfcKeysScreen::_loadDatabases() {
  _state = STATE_DATABASES;
  if (!_pickDir.length()) _pickDir = kDictDir;

  _browser.root = kDictDir;
  uint8_t n = _browser.load(this, _pickDir, ".txt", nullptr, BrowseFileView::STEM,
                            nullptr);
  if (_pickDir == kDictDir) {
    _dictItems[0] = {"Default"}; _dictItems[1] = {"Discovered"}; _dictItems[2] = {"Extended"};
    uint8_t visible = 3;
    for (uint8_t i=0;i<n;++i) {
      // discovered.txt is represented by the virtual Discovered entry above.
      if (_browser.items()[i].label == "discovered") continue;
      _dictItems[visible++]=_browser.items()[i];
    }
    setItems(_dictItems, visible);
  } else setItems(_browser.items(), n);
  render();
}

void ChameleonMfcKeysScreen::_openDatabase(const String& path, const String& name) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1600);
    return;
  }

  String content = Uni.Storage->readFile(path.c_str());
  _rowCount = 0;

  // One key per line; blank lines and '#' comments are skipped. Rows are capped
  // at kMaxRows so a large dictionary can't run past the row/label/value arrays.
  int start = 0;
  while (start < (int)content.length() && _rowCount < kMaxRows) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();

    String line = content.substring(start, nl);
    line.trim();

    if (line.length() && !line.startsWith("#")) {
      _labels[_rowCount] = String(_rowCount + 1);
      _values[_rowCount] = line;
      _rows[_rowCount]   = {_labels[_rowCount].c_str(), _values[_rowCount]};
      ++_rowCount;
    }

    start = nl + 1;
  }

  if (!_rowCount) {
    ShowStatusAction::show("No keys in file", 1600);
    return;
  }

  _viewTitle = name;
  _state     = STATE_VIEW;
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void ChameleonMfcKeysScreen::_openBuiltinDatabase(const String& id, const char* name) {
  const uint8_t (*keys)[6]=nullptr; size_t count=0;
  if (!MfcKeyStore::builtinDictionary(id,&keys,&count)) return;
  _rowCount=0;
  for(size_t i=0;i<count && _rowCount<kMaxRows;++i){_labels[_rowCount]=String(_rowCount+1);_values[_rowCount]=MfcKeyStore::keyString(keys[i]);_rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount]};++_rowCount;}
  _viewTitle=name; _state=STATE_VIEW; _scrollView.resetScroll(); _scrollView.setRows(_rows,_rowCount); render();
}

// ── screen hooks ───────────────────────────────────────

void ChameleonMfcKeysScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    _selMenu = index;
    if (index == 0) {
      Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_SHOW_KEYS));
    } else if (index == 1) {
      _pickDir = kDictDir;
      _loadDatabases();
    }
    return;
  }

  if (_state == STATE_DATABASES) {
    if (_pickDir == kDictDir && index < 3) {
      if (index == 0) _openBuiltinDatabase(MfcKeyStore::kBuiltinDefaultId, "Default");
      else if (index == 1) _openDatabase(MfcKeyStore::kDiscoveredDictionary, "Discovered");
      else _openBuiltinDatabase(MfcKeyStore::kBuiltinExtendedId, "Extended");
      return;
    }
    uint8_t fi = index;
    if (_pickDir == kDictDir) {
      uint8_t wanted = index - 3, seen = 0; bool found = false;
      for (uint8_t i=0; i<_browser.count(); ++i) {
        if (_browser.items()[i].label == "discovered") continue;
        if (seen++ == wanted) { fi = i; found = true; break; }
      }
      if (!found) return;
    }
    if (fi >= _browser.count()) return;
    const auto& e = _browser.entry(fi);
    if (e.isDir) {
      _pickDir = e.path;
      _loadDatabases();
    } else {
      _openDatabase(e.path, e.label);
    }
  }
}

void ChameleonMfcKeysScreen::onUpdate() {
  if (_state == STATE_VIEW) {
    if (Uni.Nav->wasPressed()) {
      auto d = Uni.Nav->readDirection();
      if (d == INavigation::DIR_BACK) _loadDatabases();
      else                            _scrollView.onNav(d);
    }
    return;
  }
  ListScreen::onUpdate();
}

void ChameleonMfcKeysScreen::onRender() {
  if (_state == STATE_VIEW) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void ChameleonMfcKeysScreen::onBack() {
  if (_state == STATE_VIEW) {
    _loadDatabases();
    return;
  }

  if (_state == STATE_DATABASES) {
    // Walk back up the dictionary tree; leaving the root returns to the menu.
    if (_pickDir == kDictDir || !_pickDir.length()) {
      _pickDir = "";
      _goMenu();
    } else {
      int slash = _pickDir.lastIndexOf('/');
      _pickDir  = (slash > 0) ? _pickDir.substring(0, slash) : String(kDictDir);
      if (!_pickDir.startsWith(kDictDir)) _pickDir = kDictDir;
      _loadDatabases();
    }
    return;
  }

  Screen.goBack();
}
