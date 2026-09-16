#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "utils/nfc/NfcDumpParser.h"

class NfcDumpEditorScreen : public ListScreen
{
public:
  explicit NfcDumpEditorScreen(const String& initialPath = "", bool postCreate = false)
      : _initialPath(initialPath), _postCreate(postCreate) {}
  // Takes ownership of initialDump; it must have been allocated with new[].
  NfcDumpEditorScreen(uint8_t* initialDump, size_t initialDumpLen);

  const char* title() override;

  void onInit() override;
  void onUpdate() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;
  ~NfcDumpEditorScreen() override;

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
  NfcDumpParser::Info _info;
  uint8_t* _dump = nullptr;
  size_t _dumpLen = 0;
  bool _dirty = false;

  String _infoValues[5];
  ListItem _infoItems[7];
  ListItem _actionItems[8];
  uint8_t _actionCodes[8] = {};
  uint8_t _actionCount = 0;

  void _openFiles();
  void _selectFile(uint8_t index);
  bool _confirmDiscardChanges();
  bool _loadFile(const String& path);
  void _freeDump();
  void _editMemory();
  void _editUid();
  void _editNdef();
  static bool _applyEditedNdef(void* context, const uint8_t* ndef, size_t len);
  void _showPasswordInfo();
  void _setPassword();
  void _removePassword();
  bool _saveAs();
  String _suggestedDumpName() const;
  void _showInfo();
  void _showActions();
  String _uidString() const;
};
