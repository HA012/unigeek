#include "screens/setting/ShowHideModulesScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"

void ShowHideModulesScreen::onInit() {
  for (uint8_t i = 0; i < ModuleRegistry::MOD_COUNT; i++) {
    const uint8_t id = ModuleRegistry::DISPLAY_ORDER[i];
    _ids[i] = id;
    _items[i].label    = ModuleRegistry::LABELS[id];
    _items[i].sublabel = nullptr;
  }
  setItems(_items);
  _refresh();
}

void ShowHideModulesScreen::_refresh() {
  for (uint8_t i = 0; i < ModuleRegistry::MOD_COUNT; i++) {
    const uint8_t id = _ids[i];
    _subs[i]           = ModuleRegistry::isHidden(id) ? "Hidden" : "Shown";
    _items[i].sublabel = _subs[i].c_str();
  }
  render();
}

void ShowHideModulesScreen::onItemSelected(uint8_t index) {
  if (index >= ModuleRegistry::MOD_COUNT) return;
  const uint8_t id = _ids[index];
  ModuleRegistry::setHidden(id, !ModuleRegistry::isHidden(id));
  Config.save(Uni.Storage);
  _refresh();
}

void ShowHideModulesScreen::onBack() {
  Screen.goBack();
}
