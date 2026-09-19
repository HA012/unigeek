#include "NfcScreen.h"
#include "core/ScreenManager.h"
#include "screens/utility/NdefGeneratorScreen.h"
#include "screens/utility/NdefEditorScreen.h"
#include "screens/utility/HfDumpGeneratorScreen.h"
#include "screens/utility/HfDumpEditorScreen.h"
#include "screens/utility/LfDataGeneratorScreen.h"
#include "screens/utility/LfDataEditorScreen.h"

void NfcScreen::onInit() {
  setItems(_items);
}

void NfcScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0:
      Screen.push(new HfDumpGeneratorScreen());
      break;
    case 1:
      Screen.push(new HfDumpEditorScreen());
      break;
    case 2:
      Screen.push(new LfDataGeneratorScreen());
      break;
    case 3:
      Screen.push(new LfDataEditorScreen());
      break;
    case 4:
      Screen.push(new NdefGeneratorScreen());
      break;
    case 5:
      Screen.push(new NdefEditorScreen());
      break;
  }
}
