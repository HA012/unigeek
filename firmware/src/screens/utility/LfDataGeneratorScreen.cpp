#include "LfDataGeneratorScreen.h"
#include "core/ScreenManager.h"
#include "screens/utility/LfDataEditorScreen.h"
#include "ui/actions/ShowStatusAction.h"

void LfDataGeneratorScreen::onInit() {
  _count = 0;
  for (size_t i = 0; i < LFCodec::formatCount() && _count < kMaxFormats; ++i) {
    const auto* info = LFCodec::formatAt(i);
    if (!info || LFCodec::editableFieldCount(info->protocol) == 0) continue;
    _items[_count] = {info->name};
    _protocols[_count] = info->protocol;
    ++_count;
  }
  setItems(_items, _count);
}

void LfDataGeneratorScreen::onItemSelected(uint8_t index) {
  if (index >= _count) return;
  LFCodec::DecodedData data;
  if (!LFCodec::create(_protocols[index], data)) {
    ShowStatusAction::show("Failed", 1500);
    return;
  }
  Screen.push(new LfDataEditorScreen(data, true));
}
