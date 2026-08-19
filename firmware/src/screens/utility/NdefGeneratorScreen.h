#pragma once

#include "ui/templates/ListScreen.h"

class NdefGeneratorScreen : public ListScreen
{
public:
  const char* title() override { return "Generate NDEF Record"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  static constexpr size_t MAX_NDEF_BYTES = 254;
  static constexpr const char* _nfcPath  = "/unigeek/nfc";
  static constexpr const char* _ndefPath = "/unigeek/nfc/ndefs";

  ListItem _items[5] = {
    {"Text"},
    {"URL"},
    {"Phone"},
    {"Email"},
    {"vCard"},
  };

  bool _saveNdef(const uint8_t* ndef, size_t ndefLen, const String& suggestedName);
  void _generateText();
  void _generateUrl();
  void _generatePhone();
  void _generateEmail();
  void _generateVcard();
};
