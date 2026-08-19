#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class NfcTagGeneratorScreen : public ListScreen
{
public:
  const char* title() override;

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_TAG_TYPE,
    STATE_NDEF_CONTENT,
    STATE_NDEF_TYPE,
    STATE_NDEF_FILE_SELECT,
  };

  enum TagType {
    TAG_MIFARE_CLASSIC_1K,
    TAG_NTAG215,
  };

  State _state = STATE_TAG_TYPE;
  TagType _tagType = TAG_NTAG215;

  static constexpr const char* _nfcPath   = "/unigeek/nfc";
  static constexpr const char* _ndefPath  = "/unigeek/nfc/ndefs";
  static constexpr const char* _dumpPath  = "/unigeek/nfc/dumps";
  static constexpr size_t MAX_INPUT_NDEF  = 491;

  ListItem _tagItems[2] = {
    {"MIFARE Classic"},
    {"NTAG215"},
  };

  ListItem _contentItems[3] = {
    {"Generate New"},
    {"From File"},
    {"Empty"},
  };

  ListItem _ndefTypeItems[5] = {
    {"Text"},
    {"URL"},
    {"Phone"},
    {"Email"},
    {"vCard"},
  };

  BrowseFileView _browser;
  String _ndefPickDir;

  void _goTagType();
  void _goNdefContent();
  void _goNdefType();
  void _openNdefFiles();
  void _selectNdefFile(uint8_t index);

  bool _saveTag(const uint8_t* ndef, size_t ndefLen,
                const String& suggestedName);
  bool _saveMifareClassic1K(const uint8_t* ndef, size_t ndefLen,
                            const String& suggestedName);
  bool _saveNtag215(const uint8_t* ndef, size_t ndefLen,
                    const String& suggestedName);
  void _generateMifareUid(uint8_t uid[4]);
  void _generateUid(uint8_t uid[7]);
};
