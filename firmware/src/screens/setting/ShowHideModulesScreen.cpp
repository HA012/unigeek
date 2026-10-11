#include "screens/setting/ShowHideModulesScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"

void ShowHideModulesScreen::onInit() {
  _count = 0;
  for (uint8_t i = 0; i < ModuleRegistry::MOD_COUNT; i++) {
    const uint8_t id = ModuleRegistry::DISPLAY_ORDER[i];
    if (!ModuleRegistry::isAvailable(id)) continue;
    _ids[_count] = id;
    _items[_count].label    = ModuleRegistry::LABELS[id];
    _items[_count].sublabel = nullptr;
    _count++;
  }
  setItems(_items, _count);
  _refresh();
}

void ShowHideModulesScreen::_refresh() {
  for (uint8_t i = 0; i < _count; i++) {
    const uint8_t id = _ids[i];
    _subs[i]           = ModuleRegistry::isHidden(id) ? "Hidden" : "Shown";
    _items[i].sublabel = _subs[i].c_str();
  }
  render();
}

void ShowHideModulesScreen::onItemSelected(uint8_t index) {
  if (index >= _count) return;
  const uint8_t id = _ids[index];
  ModuleRegistry::setHidden(id, !ModuleRegistry::isHidden(id));
  Config.save(Uni.Storage);
  _refresh();
}

void ShowHideModulesScreen::onBack() {
  Screen.goBack();
}
