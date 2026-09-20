#include "NfcScreen.h"
#include "core/ScreenManager.h"
#include "screens/utility/NdefToolsScreen.h"
#include "screens/utility/HfDumpToolsScreen.h"
#include "screens/utility/LfDataToolsScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "screens/utility/IdentityToolsScreen.h"

void NfcScreen::onInit() { setItems(_items); }
void NfcScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new NfcToolsScreen());
  else if (index == 1) Screen.push(new RfidToolsScreen());
}

void NfcToolsScreen::onInit() { setItems(_items); }
void NfcToolsScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new UidLibraryScreen()); break;
    case 1: Screen.push(new UidFormScreen()); break;
    case 2: Screen.push(new HfDumpLibraryScreen()); break;
    case 3: Screen.push(new HfDumpFormScreen()); break;
    case 4: Screen.push(new NdefLibraryScreen()); break;
    case 5: Screen.push(new NdefRecordFormScreen()); break;
  }
}

void RfidToolsScreen::onInit() { setItems(_items); }
void RfidToolsScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new IdLibraryScreen()); break;
    case 1: Screen.push(new IdFormScreen()); break;
    case 2: Screen.push(new LfDataLibraryScreen()); break;
    case 3: Screen.push(new LfDataFormScreen()); break;
  }
}
