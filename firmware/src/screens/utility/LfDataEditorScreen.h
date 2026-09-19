#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/rfid/LFCodec.h"

class LfDataEditorScreen : public ListScreen {
public:
  LfDataEditorScreen() = default;
  LfDataEditorScreen(const LFCodec::DecodedData& data, bool newUnsaved)
      : _data(data), _newUnsaved(newUnsaved), _loaded(true), _dirty(newUnsaved) {}
  const char* title() override;
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;
private:
  enum State { STATE_FILE_SELECT, STATE_DETAILS, STATE_ACTIONS };
  static constexpr const char* kPath = "/unigeek/rfid";
  State _state = STATE_FILE_SELECT;
  BrowseFileView _browser;
  String _pickDir;
  String _filePath;
  LFCodec::DecodedData _data;
  bool _newUnsaved = false;
  bool _loaded = false;
  bool _dirty = false;

  ScrollListView _details;
  static constexpr uint8_t kMaxRows = 10;
  ScrollListView::Row _rows[kMaxRows];
  String _labels[kMaxRows], _values[kMaxRows];
  uint8_t _rowCount = 0;
  ListItem _actions[8];
  uint8_t _actionCount = 0;

  void _openFiles();
  void _selectFile(uint8_t index);
  bool _loadFile(const String& path);
  void _showDetails();
  void _showActions();
  void _editField(size_t index);
  String _fieldValue(const LFCodec::FieldInfo& field) const;
  bool _save();
  bool _saveAs();
  bool _writeFile(const String& path);
  bool _confirmDiscard();
  String _suggestedName() const;
};
