#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/nfc/HfDumpParser.h"

class HfDumpEditorScreen : public ListScreen
{
public:
  explicit HfDumpEditorScreen(const String& initialPath = "", bool postCreate = false)
      : _initialPath(initialPath), _postCreate(postCreate) {}
  // Takes ownership of initialDump; it must have been allocated with new[].
  HfDumpEditorScreen(uint8_t* initialDump, size_t initialDumpLen);

  const char* title() override;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;
  ~HfDumpEditorScreen() override;

private:
  enum State {
    STATE_FILE_SELECT,
    STATE_DUMP_INFO,
    STATE_ACTIONS,
  };

  static constexpr const char* _dumpPath = "/unigeek/nfc/dumps";
  static constexpr size_t MAX_DUMP_BYTES = 4096;

  State _state = STATE_FILE_SELECT;
  BrowseFileView _browser;
  String _pickDir;
  String _filePath;
  String _initialPath;
  bool _postCreate = false;
  bool _newUnsaved = false;
  HfDumpParser::Info _info;
  uint8_t* _dump = nullptr;
  size_t _dumpLen = 0;
  bool _dirty = false;
  bool _returnToInfoAfterChild = false;
  bool _holdFired = false;

  ScrollListView _infoView;
  static constexpr uint8_t INFO_ROW_MAX = 7;
  ScrollListView::Row _infoRows[INFO_ROW_MAX];
  String _infoLabels[INFO_ROW_MAX];
  String _infoValues[INFO_ROW_MAX];
  uint8_t _infoRowCount = 0;
  ListItem _actionItems[8];
  uint8_t _actionCodes[8] = {};
  uint8_t _actionCount = 0;

  void _openFiles();
  void _selectFile(uint8_t index);
  bool _loadFile(const String& path);
  void _freeDump();
  void _editMemory();
  void _editUid();
  void _editNdef();
  static bool _applyEditedNdef(void* context, const uint8_t* ndef, size_t len);
  void _showPasswordInfo();
  void _setPassword();
  void _removePassword();
  bool _save();
  bool _saveAs();
  bool _writeFile(const String& path);
  String _suggestedDumpName() const;
  void _showInfo();
  void _showActions();
  String _uidString() const;
};
