#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include "ui/views/BrowseFileView.h"

class SshClientScreen : public ListScreen
{
public:
  const char* title() override { return "SSH Client"; }
  ~SshClientScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t {
    STATE_CONFIG,
    STATE_SELECT_AUTH,
    STATE_SELECT_KEY,
    STATE_CONNECTING,
    STATE_OUTPUT
  };
  enum AuthMode : uint8_t { AUTH_PASSWORD, AUTH_PRIVATE_KEY };
  enum WorkerState : uint8_t {
    WORKER_IDLE,
    WORKER_CONNECTING,
    WORKER_RUNNING,
    WORKER_FAILED,
    WORKER_CLOSED
  };
  enum AnsiParseState : uint8_t { ANSI_DATA, ANSI_ESC, ANSI_CSI, ANSI_OSC, ANSI_OSC_ESC };

  static constexpr int MAX_PARTIAL_LEN       = 160;
  static constexpr int MAX_TRANSCRIPT_CHARS  = 4096;
  static constexpr int MAX_SHARED_RX_CHARS   = 4096;
  static constexpr int PAD                   = 4;
  static constexpr int FOOTER_H              = 12;
  static constexpr uint32_t SSH_TASK_STACK_SIZE = 24576;

  State _state = STATE_CONFIG;

  String _host;
  String _username;
  String _password;
  String _keyPath;
  String _keyData;
  AuthMode _authMode = AUTH_PASSWORD;
  int    _port = 22;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};
  char _userLabel[32] = {};
  char _authLabel[20] = "Password";
  char _keyLabel[32] = {};
  ListItem _items[6];
  BrowseFileView _browser;
  String _browsePath = "/unigeek/wifi/ssh";
  ListItem _authItems[2] = {
    {"Password", nullptr},
    {"Private Key", nullptr},
  };

  TaskHandle_t _sshTask = nullptr;
  SemaphoreHandle_t _ioMutex = nullptr;
  volatile WorkerState _workerState = WORKER_IDLE;
  volatile bool _stopRequested = false;
  String _workerRx;
  String _workerTx;
  String _workerError;

  TextScrollView _outputView;
  String _transcript;
  String _partialLine;
  int    _lineCursor = 0;
  String _ansiParams;
  AnsiParseState _ansiState = ANSI_DATA;
  bool _remoteClosed = false;
  bool _followOutput = true;

  void _updateLabels();
  void _rebuildItems();
  void _configHost();
  void _configPort();
  void _configUsername();
  void _auth();
  void _selectAuth(uint8_t index);
  void _openKeyPicker();
  void _loadKeyDir(const String& path);
  void _selectKey(uint8_t index);
  void _connect();
  void _closeConnection(bool returnToConfig = true);

  bool _startSshWorker();
  static void _sshWorkerEntry(void* arg);
  void _sshWorker();
  void _setWorkerError(const char* message);
  void _drainWorkerRx();
  void _openCommandInput();
  void _sendCommand(const String& command);

  void _appendByte(uint8_t c);
  void _handleAnsiCsi(uint8_t finalByte);
  int  _ansiParam(int index, int defaultValue) const;
  void _putLineChar(char c);
  void _commitPartial();
  void _pushOutputLine(const String& line);
  void _trimTranscript();
  void _clearOutput();

  int _terminalCols();
  int _terminalRows();
  void _renderConnecting();
  void _renderOutput();
};
