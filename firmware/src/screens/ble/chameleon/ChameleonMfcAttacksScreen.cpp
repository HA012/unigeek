#include "ChameleonMfcAttacksScreen.h"
#include "ChameleonMfcBackdoorScreen.h"
#include "ChameleonMfcDarksideScreen.h"
#include "ChameleonMfcMfkey32Screen.h"
#include "ChameleonMfcScreen.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/ble/ChameleonClient.h"

void ChameleonMfcAttacksScreen::onInit() {
  _items[0] = {"Dictionary"};
  _items[1] = {"Backdoor Assisted SEN"};
  _items[2] = {"Darkside"};
  _items[3] = {"Nested"};
  _items[4] = {"Static Nested"};
  _items[5] = {"Attack Chain"};
  _items[6] = {"MFKey32"};
  setItems(_items);
}


void ChameleonMfcAttacksScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_DICTIONARY));     break;
    case 1:
      Screen.push(new ChameleonMfcBackdoorScreen());
      break;
    case 2:
      Screen.push(new ChameleonMfcDarksideScreen());
      break;
    case 3:
      Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_NESTED));
      break;
    case 4:
      Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_STATIC_NESTED));
      break;
    case 5:
      Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_RECOVER));
      break;
    case 6: {
      auto& c = ChameleonClient::get();
      ChameleonClient::SlotTypes types[8] = {};
      if (!c.getSlotTypes(types)) {
        ShowStatusAction::show("Could not read slots", 1600);
        render();
        break;
      }
      InputSelectAction::Option opts[8];
      String labels[8], vals[8];
      for (uint8_t i = 0; i < 8; ++i) {
        labels[i] = String("Slot ") + (i + 1) + " - " + ChameleonClient::tagTypeName(types[i].hfType);
        vals[i] = String(i);
        opts[i] = {labels[i].c_str(), vals[i].c_str()};
      }
      const char* r = InputSelectAction::popup("MFKey32 slot", opts, 8, nullptr);
      if (!r) { render(); break; }
      const uint8_t slot = (uint8_t)atoi(r);
      if (slot >= 8) { render(); break; }
      const uint16_t t = types[slot].hfType;
      if (!(t == 1000 || t == 1001 || t == 1002 || t == 1003)) {
        ShowStatusAction::show(t == 0 ? "Empty slot" : "Tag not supported", 1400);
        render();
        break;
      }
      Screen.push(new ChameleonMfcMfkey32Screen(slot));
      break;
    }
  }
}
