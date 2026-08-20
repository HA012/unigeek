#pragma once

#include "ui/templates/ListScreen.h"

class NdefGeneratorScreen : public ListScreen
{
public:
  static constexpr size_t MAX_NDEF_BYTES = 254;

  const char* title() override { return "New NDEF Record"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

  // Shared interactive NDEF creation flow. Used both by this screen (save .ndef)
  // and by Generate NFC Tag (embed directly into a tag image).
  static bool buildRecordInteractive(uint8_t index,
                                     uint8_t* out, size_t& outLen, size_t maxLen,
                                     String& suggestedName);

private:
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
};
