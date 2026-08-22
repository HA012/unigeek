#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"

class NdefGeneratorScreen : public ListScreen
{
public:
  static constexpr size_t MAX_NDEF_BYTES = 254;

  const char* title() override;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

  // Shared interactive NDEF creation flow. Used both by this screen (save .ndef)
  // and by Generate Dump (embed directly into a dump image).
  static bool buildRecordInteractive(uint8_t index,
                                     uint8_t* out, size_t& outLen, size_t maxLen,
                                     String& suggestedName);

private:
  enum State {
    STATE_TYPE_SELECT,
    STATE_PREVIEW,
  };

  static constexpr const char* _nfcPath  = "/unigeek/nfc";
  static constexpr const char* _ndefPath = "/unigeek/nfc/ndefs";
  static constexpr size_t MAX_ROWS = 64;

  State _state = STATE_TYPE_SELECT;

  ListItem _items[5] = {
    {"Text"},
    {"URL"},
    {"Phone"},
    {"Email"},
    {"vCard"},
  };

  ScrollListView _scrollView;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;

  uint8_t _previewNdef[MAX_NDEF_BYTES] = {};
  size_t _previewNdefLen = 0;
  String _previewSuggestedName;

  void _showPreview(const uint8_t* ndef, size_t ndefLen);
  void _resetRows();
  void _pushRow(const String& label, const String& value);
  void _pushWrappedRow(const String& label, const String& value);

  bool _saveNdef(const uint8_t* ndef, size_t ndefLen, const String& suggestedName);
};
