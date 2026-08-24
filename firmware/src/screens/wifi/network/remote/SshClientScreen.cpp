#include "SshClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include <libssh/libssh.h>

SshClientScreen::~SshClientScreen() {
  _closeConnection(false);

  // The worker owns libssh objects. Do not destroy synchronization primitives
  // until it has finished its cleanup.
  uint32_t start = millis();
  while (_sshTask && millis() - start < 3000) {
    delay(10);
  }

  if (_ioMutex && !_sshTask) {
    vSemaphoreDelete(_ioMutex);
    _ioMutex = nullptr;
  }
}

void SshClientScreen::onInit() {
  _outputView.setWrapMode(TextScrollView::WRAP_CHARACTER);
  if (!_ioMutex) _ioMutex = xSemaphoreCreateMutex();
  _updateLabels();
  _rebuildItems();
}

void SshClientScreen::onUpdate() {
  if (_state == STATE_CONNECTING) {
    if (_workerState == WORKER_RUNNING) {
      _password = "";
      _clearOutput();
      _remoteClosed = false;
      _state = STATE_OUTPUT;
      _followOutput = true;
      render();
      return;
    }

    if (_workerState == WORKER_FAILED) {
      String error = "SSH connection failed";
      if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_workerError.length() > 0) error = _workerError;
        xSemaphoreGive(_ioMutex);
      }
      _password = "";
      _workerState = WORKER_IDLE;
      _state = STATE_CONFIG;
      ShowStatusAction::show(error.c_str(), 1800);
      render();
      return;
    }

    if (_workerState == WORKER_CLOSED) {
      _password = "";
      _workerState = WORKER_IDLE;
      _state = STATE_CONFIG;
      _updateLabels();
      _rebuildItems();
      render();
      return;
    }

    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      // Request cancellation, but stay in CONNECTING until the worker has
      // actually released its libssh objects.
      _stopRequested = true;
    }
    return;
  }

  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  _drainWorkerRx();

  if (!_remoteClosed && (_workerState == WORKER_CLOSED || _workerState == WORKER_FAILED)) {
    _remoteClosed = true;
    if (_partialLine.length() > 0) _commitPartial();

    if (_workerState == WORKER_FAILED) {
      String error = "SSH session error";
      if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_workerError.length() > 0) error = _workerError;
        xSemaphoreGive(_ioMutex);
      }
      _pushOutputLine("[" + error + "]");
    } else {
      _pushOutputLine("[Connection closed]");
    }
    render();
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK) {
    _closeConnection(true);
    return;
  }

  if (dir == INavigation::DIR_PRESS && !_remoteClosed) {
    _openCommandInput();
    return;
  }

  if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN ||
      dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
    if (_outputView.onNav(dir)) _followOutput = _outputView.isAtBottom();
  }
}

void SshClientScreen::onRender() {
  if (_state == STATE_CONFIG) {
    ListScreen::onRender();
    return;
  }
  if (_state == STATE_CONNECTING) {
    _renderConnecting();
    return;
  }
  _renderOutput();
}

void SshClientScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _configHost();     break;
    case 1: _configPort();     break;
    case 2: _configUsername(); break;
    case 3: _auth();           break;
    case 4: _connect();        break;
    default: break;
  }
}

void SshClientScreen::onBack() {
  if (_state == STATE_CONNECTING) {
    _stopRequested = true;
    return;
  }
  if (_state == STATE_OUTPUT) _closeConnection(true);
  else Screen.goBack();
}

void SshClientScreen::_updateLabels() {
  if (_host.length() == 0) snprintf(_hostLabel, sizeof(_hostLabel), "-");
  else                     _host.toCharArray(_hostLabel, sizeof(_hostLabel));

  if (_username.length() == 0) snprintf(_userLabel, sizeof(_userLabel), "-");
  else                         _username.toCharArray(_userLabel, sizeof(_userLabel));

  snprintf(_authLabel, sizeof(_authLabel), "%s",
           _password.length() > 0 ? "Password *" : "Password");

  snprintf(_portLabel, sizeof(_portLabel), "%d", _port);
}

void SshClientScreen::_rebuildItems() {
  uint8_t sel = _selectedIndex;
  _items[0] = {"Host", _hostLabel};
  _items[1] = {"Port", _portLabel};
  _items[2] = {"Username", _userLabel};
  _items[3] = {"Auth", _authLabel};
  _items[4] = {"Connect", nullptr};
  setItems(_items, 5, sel);
}

void SshClientScreen::_configHost() {
  String initial = _host;
  if (initial.length() == 0 && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    initial = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
  }

  String value = InputTextAction::popup("Host", initial, InputTextAction::INPUT_IP_ADDRESS);
  if (!InputTextAction::wasCancelled() && value.length() > 0) {
    _host = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port);
  if (!InputNumberAction::wasCancelled()) {
    _port = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_configUsername() {
  String value = InputTextAction::popup("Username", _username, InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled() && value.length() > 0) {
    _username = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_auth() {
  String value = InputTextAction::popup("Password", "", InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled()) {
    _password = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_connect() {
  if (_host.length() == 0) {
    ShowStatusAction::show("Host required", 1200);
    render();
    return;
  }
  if (_username.length() == 0) {
    ShowStatusAction::show("Username required", 1200);
    render();
    return;
  }
  if (_port < 1 || _port > 65535) {
    ShowStatusAction::show("Port required", 1200);
    render();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected", 1500);
    render();
    return;
  }

  if (_password.length() == 0) {
    String value = InputTextAction::popup("Password", "", InputTextAction::INPUT_TEXT);
    if (InputTextAction::wasCancelled()) {
      render();
      return;
    }
    _password = value;
  }

  if (!_ioMutex) _ioMutex = xSemaphoreCreateMutex();
  if (!_ioMutex) {
    ShowStatusAction::show("SSH mutex failed", 1500);
    return;
  }

  if (!_startSshWorker()) {
    _password = "";
    ShowStatusAction::show("SSH task failed", 1500);
    render();
    return;
  }

  _state = STATE_CONNECTING;
  render();
}

bool SshClientScreen::_startSshWorker() {
  if (_sshTask) return false;

  _stopRequested = false;
  _workerState = WORKER_CONNECTING;

  if (xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _workerRx = "";
    _workerTx = "";
    _workerError = "";
    xSemaphoreGive(_ioMutex);
  }

  BaseType_t ok;
#if defined(SOC_CPU_CORES_NUM) && SOC_CPU_CORES_NUM > 1
  ok = xTaskCreatePinnedToCore(
    _sshWorkerEntry, "UG SSH", SSH_TASK_STACK_SIZE, this, 1, &_sshTask, 1);
#else
  ok = xTaskCreate(
    _sshWorkerEntry, "UG SSH", SSH_TASK_STACK_SIZE, this, 1, &_sshTask);
#endif

  if (ok != pdPASS) {
    _sshTask = nullptr;
    _workerState = WORKER_IDLE;
    return false;
  }
  return true;
}

void SshClientScreen::_sshWorkerEntry(void* arg) {
  static_cast<SshClientScreen*>(arg)->_sshWorker();
}

void SshClientScreen::_setWorkerError(const char* message) {
  if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    _workerError = message ? message : "SSH failed";
    xSemaphoreGive(_ioMutex);
  }
  _workerState = WORKER_FAILED;
}

void SshClientScreen::_sshWorker() {
  ssh_session session = nullptr;
  ssh_channel channel = nullptr;

  // Copy connection settings before entering libssh. The CONFIG UI is no longer
  // editable while STATE_CONNECTING is active.
  String host = _host;
  String user = _username;
  String password = _password;
  int port = _port;
  int cols = _terminalCols();
  int rows = _terminalRows();

  session = ssh_new();
  if (!session) {
    _setWorkerError("SSH session failed");
    goto cleanup;
  }

  if (ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str()) != SSH_OK ||
      ssh_options_set(session, SSH_OPTIONS_USER, user.c_str()) != SSH_OK ||
      ssh_options_set(session, SSH_OPTIONS_PORT, &port) != SSH_OK) {
    _setWorkerError("SSH options failed");
    goto cleanup;
  }

  if (_stopRequested) goto cleanup;

  if (ssh_connect(session) != SSH_OK) {
    _setWorkerError("SSH connect failed");
    goto cleanup;
  }

  if (_stopRequested) goto cleanup;

  if (ssh_userauth_password(session, nullptr, password.c_str()) != SSH_AUTH_SUCCESS) {
    _setWorkerError("SSH authentication failed");
    goto cleanup;
  }

  // Clear the worker's private copy as soon as authentication is complete.
  password = "";

  channel = ssh_channel_new(session);
  if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
    _setWorkerError("SSH channel failed");
    goto cleanup;
  }

  if (ssh_channel_request_pty_size(channel, "vt100", cols, rows) != SSH_OK) {
    _setWorkerError("SSH PTY failed");
    goto cleanup;
  }

  if (ssh_channel_request_shell(channel) != SSH_OK) {
    _setWorkerError("SSH shell failed");
    goto cleanup;
  }

  ssh_set_blocking(session, 0);
  _workerState = WORKER_RUNNING;

  while (!_stopRequested) {
    bool activity = false;
    char buf[256];

    // TX queue: UI only appends complete commands, so moving the whole String is
    // enough and keeps libssh calls entirely inside this worker.
    String tx;
    if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (_workerTx.length() > 0) {
        tx = _workerTx;
        _workerTx = "";
      }
      xSemaphoreGive(_ioMutex);
    }

    if (tx.length() > 0) {
      int sent = 0;
      while (sent < (int)tx.length() && !_stopRequested) {
        int n = ssh_channel_write(channel, tx.c_str() + sent, tx.length() - sent);
        if (n == SSH_ERROR) {
          _setWorkerError("SSH write failed");
          goto cleanup;
        }
        if (n <= 0) break;
        sent += n;
      }
      activity = true;
    }

    for (int stream = 0; stream <= 1; ++stream) {
      while (!_stopRequested) {
        int n = ssh_channel_read_nonblocking(channel, buf, sizeof(buf), stream);
        if (n == SSH_ERROR) {
          _setWorkerError("SSH read failed");
          goto cleanup;
        }
        if (n <= 0) break;

        if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          if ((int)_workerRx.length() + n > MAX_SHARED_RX_CHARS) {
            int drop = (int)_workerRx.length() + n - MAX_SHARED_RX_CHARS;
            _workerRx.remove(0, min(drop, (int)_workerRx.length()));
          }
          _workerRx.concat(buf, n);
          xSemaphoreGive(_ioMutex);
        }
        activity = true;
      }
    }

    if (!ssh_is_connected(session) || !ssh_channel_is_open(channel) ||
        ssh_channel_is_eof(channel)) {
      break;
    }

    if (!activity) vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (_workerState != WORKER_FAILED) _workerState = WORKER_CLOSED;

cleanup:
  if (channel) {
    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    channel = nullptr;
  }

  if (session) {
    ssh_disconnect(session);
    ssh_free(session);
    session = nullptr;
  }

  password = "";
  _sshTask = nullptr;
  vTaskDelete(nullptr);
}

void SshClientScreen::_closeConnection(bool returnToConfig) {
  _stopRequested = true;

  // When the session is already running, the worker is non-blocking and should
  // exit promptly. Avoid deleting it from the UI task because libssh objects
  // belong exclusively to the worker.
  uint32_t start = millis();
  while (_sshTask && _workerState == WORKER_RUNNING && millis() - start < 500) {
    delay(5);
  }

  _password = "";
  _remoteClosed = false;

  if (returnToConfig) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildItems();
    render();
  }
}

void SshClientScreen::_openCommandInput() {
  String command = InputTextAction::popup("Command", "", InputTextAction::INPUT_TEXT);

  if (!InputTextAction::wasCancelled()) _sendCommand(command);

  _drainWorkerRx();
  render();
}

void SshClientScreen::_sendCommand(const String& command) {
  if (_remoteClosed || _workerState != WORKER_RUNNING || !_ioMutex) return;

  String outgoing = command;
  outgoing += "\r\n";

  if (xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _workerTx += outgoing;
    xSemaphoreGive(_ioMutex);
  }
}

void SshClientScreen::_drainWorkerRx() {
  if (!_ioMutex) return;

  String data;
  if (xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (_workerRx.length() > 0) {
      data = _workerRx;
      _workerRx = "";
    }
    xSemaphoreGive(_ioMutex);
  }

  if (data.length() == 0) return;

  for (int i = 0; i < (int)data.length(); ++i) {
    _appendByte((uint8_t)data[i]);
  }
  render();
}

void SshClientScreen::_appendByte(uint8_t c) {
  switch (_ansiState) {
    case ANSI_ESC:
      _ansiParams = "";
      if (c == '[')      _ansiState = ANSI_CSI;
      else if (c == ']') _ansiState = ANSI_OSC;
      else               _ansiState = ANSI_DATA;
      return;

    case ANSI_CSI:
      if (c >= 0x40 && c <= 0x7e) {
        _handleAnsiCsi(c);
        _ansiState = ANSI_DATA;
        _ansiParams = "";
      } else if (_ansiParams.length() < 24 &&
                 ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '>')) {
        _ansiParams += (char)c;
      }
      return;

    case ANSI_OSC:
      if (c == 0x07)      _ansiState = ANSI_DATA;
      else if (c == 0x1b) _ansiState = ANSI_OSC_ESC;
      return;

    case ANSI_OSC_ESC:
      _ansiState = (c == '\\') ? ANSI_DATA : ANSI_OSC;
      return;

    case ANSI_DATA:
    default:
      if (c == 0x1b) {
        _ansiState = ANSI_ESC;
        return;
      }
      break;
  }

  if (c == '\r') {
    _lineCursor = 0;
    return;
  }

  if (c == '\n') {
    _commitPartial();
    return;
  }

  if (c == '\b' || c == 0x7f) {
    if (_lineCursor > 0) _lineCursor--;
    return;
  }

  if (c == '\t') {
    int spaces = 8 - (_lineCursor % 8);
    while (spaces-- > 0) _putLineChar(' ');
  } else if (c >= 0x20 && c <= 0x7e) {
    _putLineChar((char)c);
  }

  if ((int)_partialLine.length() >= MAX_PARTIAL_LEN) _commitPartial();
}

void SshClientScreen::_putLineChar(char c) {
  if (_lineCursor < 0) _lineCursor = 0;
  if (_lineCursor > (int)_partialLine.length()) {
    while ((int)_partialLine.length() < _lineCursor) _partialLine += ' ';
  }

  if (_lineCursor < (int)_partialLine.length())
    _partialLine.setCharAt(_lineCursor, c);
  else
    _partialLine += c;

  _lineCursor++;
}

int SshClientScreen::_ansiParam(int index, int defaultValue) const {
  int current = 0;
  int value = 0;
  bool haveDigit = false;

  for (int i = 0; i <= (int)_ansiParams.length(); i++) {
    char c = (i < (int)_ansiParams.length()) ? _ansiParams[i] : ';';
    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      haveDigit = true;
    } else if (c == ';') {
      if (current == index) return haveDigit ? value : defaultValue;
      current++;
      value = 0;
      haveDigit = false;
    }
  }
  return defaultValue;
}

void SshClientScreen::_handleAnsiCsi(uint8_t finalByte) {
  int n = _ansiParam(0, 1);
  if (n < 1) n = 1;

  switch (finalByte) {
    case 'm':
      break;
    case 'C':
      _lineCursor = min((int)_partialLine.length(), _lineCursor + n);
      break;
    case 'D':
      _lineCursor = max(0, _lineCursor - n);
      break;
    case 'G':
      _lineCursor = max(0, n - 1);
      if (_lineCursor > MAX_PARTIAL_LEN) _lineCursor = MAX_PARTIAL_LEN;
      break;
    case 'K': {
      int mode = _ansiParam(0, 0);
      if (mode == 2) {
        _partialLine = "";
        _lineCursor = 0;
      } else if (mode == 1) {
        int upto = min(_lineCursor + 1, (int)_partialLine.length());
        for (int i = 0; i < upto; i++) _partialLine.setCharAt(i, ' ');
      } else if (_lineCursor < (int)_partialLine.length()) {
        _partialLine.remove(_lineCursor);
      }
      break;
    }
    default:
      break;
  }
}

void SshClientScreen::_commitPartial() {
  _pushOutputLine(_partialLine);
  _partialLine = "";
  _lineCursor = 0;
}

void SshClientScreen::_pushOutputLine(const String& line) {
  _transcript += line;
  _transcript += '\n';
  _trimTranscript();
}

void SshClientScreen::_trimTranscript() {
  if ((int)_transcript.length() <= MAX_TRANSCRIPT_CHARS) return;
  int excess = (int)_transcript.length() - MAX_TRANSCRIPT_CHARS;
  int cut = _transcript.indexOf('\n', excess);
  if (cut >= 0) _transcript.remove(0, cut + 1);
  else          _transcript.remove(0, excess);
}

void SshClientScreen::_clearOutput() {
  _transcript = "";
  _partialLine = "";
  _lineCursor = 0;
  _ansiParams = "";
  _ansiState = ANSI_DATA;
  _followOutput = true;
  _outputView.setContent("");
}

int SshClientScreen::_terminalCols() {
  const int textW = max(1, bodyW() - 3 - 8);
  return max(8, textW / 6);
}

int SshClientScreen::_terminalRows() {
  const int outputH = max(1, bodyH() - PAD - 11 - 4 - FOOTER_H - 2);
  return max(1, outputH / 11);
}

void SshClientScreen::_renderConnecting() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX();
  const int y = bodyY();
  const int w = bodyW();
  const int h = bodyH();

  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  String msg = "Connecting to " + _host + ":" + String(_port) + "...";
  lcd.drawString(msg.c_str(), x + w / 2, y + h / 2);
  lcd.setTextDatum(TL_DATUM);
}

void SshClientScreen::_renderOutput() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX();
  const int y = bodyY();
  const int w = bodyW();
  const int h = bodyH();

  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);

  int cy = y + PAD;

  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  String connected = "Connected to " + _host + ":" + String(_port);
  lcd.drawString(connected.c_str(), x + PAD, cy);
  cy += 11;

  lcd.drawFastHLine(x + PAD, cy, w - PAD * 2, TFT_DARKGREY);
  cy += 4;

  const int footerY = y + h - FOOTER_H;
  const int outputBottom = footerY - 2;

  String display = _transcript;
  if (_partialLine.length() > 0) display += _partialLine;
  if (display.length() == 0 && !_remoteClosed) display = "Waiting for data...";

  _outputView.updateContent(display, _followOutput);
  _outputView.render(x, cy, w, max(1, outputBottom - cy));

  lcd.drawFastHLine(x + PAD, footerY - 2, w - PAD * 2, TFT_DARKGREY);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString(_remoteClosed ? "BACK: Exit" : "PRESS: Cmd | BACK: Exit",
                 x + w / 2, footerY + FOOTER_H / 2 - 1);
  lcd.setTextDatum(TL_DATUM);
}
