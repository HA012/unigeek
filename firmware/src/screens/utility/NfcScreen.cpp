#include "NfcScreen.h"
#include "core/ScreenManager.h"
#include "screens/utility/NdefGeneratorScreen.h"
#include "screens/utility/NdefEditorScreen.h"
#include "screens/utility/NfcDumpGeneratorScreen.h"
#include "screens/utility/NfcDumpEditorScreen.h"

void NfcScreen::onInit() {
  setItems(_items);
}

void NfcScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0:
      Screen.push(new NfcDumpGeneratorScreen());
      break;
    case 1:
      Screen.push(new NfcDumpEditorScreen());
      break;
    case 2:
      Screen.push(new NdefGeneratorScreen());
      break;
    case 3:
      Screen.push(new NdefEditorScreen());
      break;
  }
}
