#include "TelnetClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"

TelnetClientScreen::~TelnetClientScreen() {
  _closeConnection(false);
}

void TelnetClientScreen::onInit() {
  _updateLabels();
  _rebuildItems();
}

void TelnetClientScreen::onUpdate() {
  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  // OUTPUT screen: keep draining Telnet continuously while the user reads it.
  _drainSocket();

  if (!_remoteClosed && !_client.connected()) {
    // Preserve the final output on-screen instead of immediately returning to
    // CONFIG. This lets the user inspect a service that closes after replying.
    _remoteClosed = true;
    if (_partialLine.length() > 0) _commitPartial();
    _pushOutputLine("[Connection closed]");
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
    if (_outputView.onNav(dir)) {
      _followOutput = _outputView.isAtBottom();
    }
  }
}

void TelnetClientScreen::onRender() {
  if (_state == STATE_CONFIG) {
    ListScreen::onRender();
    return;
  }

  _renderOutput();
}

void TelnetClientScreen::onBack() {
  if (_state == STATE_OUTPUT) {
    _closeConnection(true);
  } else {
    Screen.goBack();
  }
}

void TelnetClientScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _configHost(); break;
    case 1: _configPort(); break;
    case 2: _connect();    break;
    default: break;
  }
}

void TelnetClientScreen::_updateLabels() {
  if (_host.length() == 0) snprintf(_hostLabel, sizeof(_hostLabel), "tap to set");
  else                     _host.toCharArray(_hostLabel, sizeof(_hostLabel));

  if (_port <= 0) snprintf(_portLabel, sizeof(_portLabel), "tap to set");
  else            snprintf(_portLabel, sizeof(_portLabel), "%d", _port);
}

void TelnetClientScreen::_rebuildItems() {
  uint8_t sel = _selectedIndex;
  _items[0] = {"Host", _hostLabel};
  _items[1] = {"Port", _portLabel};
  _items[2] = {"Connect", nullptr};
  setItems(_items, 3, sel);
}

void TelnetClientScreen::_configHost() {
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

void TelnetClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port > 0 ? _port : 0);
  if (!InputNumberAction::wasCancelled()) {
    _port = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void TelnetClientScreen::_connect() {
  if (_host.length() == 0) {
    ShowStatusAction::show("Host required", 1200);
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

  ShowStatusAction::show("Connecting...", 0);
  _client.stop();

  if (!_client.connect(_host.c_str(), (uint16_t)_port, 5000)) {
    ShowStatusAction::show("Connection failed", 1500);
    render();
    return;
  }

  _clearOutput();
  _remoteClosed = false;
  _tnState = TN_DATA;
  _tnCommand = 0;
  _tnSbHasOption = false;
  _ansiState = ANSI_DATA;
  _state = STATE_OUTPUT;
  _followOutput = true;
  render();
}

void TelnetClientScreen::_closeConnection(bool returnToConfig) {
  if (_client) _client.stop();
  _remoteClosed = false;

  if (returnToConfig) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildItems();
    render();
  }
}

void TelnetClientScreen::_openCommandInput() {
  // Always begin with an empty command. EXIT cancels and returns to OUTPUT;
  // BACK remains owned by InputTextAction (ABC <-> 123 in text mode).
  String command = InputTextAction::popup("Command", "", InputTextAction::INPUT_TEXT);

  if (!InputTextAction::wasCancelled()) {
    _sendCommand(command);
  }

  // Data may have arrived while the modal keyboard was open.
  _drainSocket();
  render();
}

void TelnetClientScreen::_sendCommand(const String& command) {
  if (_remoteClosed || !_client.connected()) return;

  // Telnet command mode uses conventional CRLF line termination.
  _pushOutputLine(String("> ") + command);
  _followOutput = true;

  if (command.length() > 0) {
    _client.write(reinterpret_cast<const uint8_t*>(command.c_str()), command.length());
  }
  _client.write((uint8_t)'\r');
  _client.write((uint8_t)'\n');
}

void TelnetClientScreen::_sendTelnetReply(uint8_t command, uint8_t option) {
  uint8_t reply[3] = {IAC, command, option};
  _client.write(reply, sizeof(reply));
}

void TelnetClientScreen::_handleNegotiation(uint8_t command, uint8_t option) {
  // Conservative client negotiation. Accept server ECHO and suppress-go-ahead;
  // reject options we do not implement. For DO SGA, advertise WILL SGA.
  if (command == WILL) {
    if (option == OPT_ECHO || option == OPT_SGA) _sendTelnetReply(DO, option);
    else                                         _sendTelnetReply(DONT, option);
  } else if (command == DO) {
    if (option == OPT_SGA) _sendTelnetReply(WILL, option);
    else                   _sendTelnetReply(WONT, option);
  }
  // DONT/WONT require no positive response for this minimal client.
}

void TelnetClientScreen::_processTelnetByte(uint8_t c) {
  switch (_tnState) {
    case TN_DATA:
      if (c == IAC) _tnState = TN_IAC;
      else          _appendByte(c);
      break;

    case TN_IAC:
      if (c == IAC) {
        // Escaped 0xFF data byte. Text-mode TO cannot render it, so ignore.
        _tnState = TN_DATA;
      } else if (c == WILL || c == WONT || c == DO || c == DONT) {
        _tnCommand = c;
        _tnState = TN_NEGOTIATE;
      } else if (c == SB) {
        _tnSbHasOption = false;
        _tnState = TN_SB;
      } else {
        // Other one-byte Telnet commands (NOP, GA, etc.) are ignored.
        _tnState = TN_DATA;
      }
      break;

    case TN_NEGOTIATE:
      _handleNegotiation(_tnCommand, c);
      _tnState = TN_DATA;
      break;

    case TN_SB:
      if (!_tnSbHasOption) {
        _tnSbOption = c;
        _tnSbHasOption = true;
      } else if (c == IAC) {
        _tnState = TN_SB_IAC;
      }
      // Subnegotiation payload is intentionally ignored in v1.
      break;

    case TN_SB_IAC:
      if (c == SE) {
        _tnState = TN_DATA;
        _tnSbHasOption = false;
      } else if (c != IAC) {
        _tnState = TN_SB;
      } else {
        _tnState = TN_SB;
      }
      break;
  }
}

void TelnetClientScreen::_drainSocket() {
  bool gotData = false;

  while (_client.available()) {
    uint8_t buf[128];
    int n = _client.read(buf, sizeof(buf));
    if (n <= 0) break;

    gotData = true;
    for (int i = 0; i < n; i++) _processTelnetByte(buf[i]);
  }

  if (gotData) render();
}

void TelnetClientScreen::_appendByte(uint8_t c) {
  // Strip ANSI/VT100 control sequences in v1 instead of rendering their
  // printable payload (e.g. "[1;32m"). Terminal interpretation comes later.
  switch (_ansiState) {
    case ANSI_ESC:
      if (c == '[')      _ansiState = ANSI_CSI;
      else if (c == ']') _ansiState = ANSI_OSC;
      else               _ansiState = ANSI_DATA;
      return;

    case ANSI_CSI:
      // CSI ends at the first final byte in 0x40..0x7E.
      if (c >= 0x40 && c <= 0x7e) _ansiState = ANSI_DATA;
      return;

    case ANSI_OSC:
      // OSC terminates on BEL or ST (ESC \).
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

  if (c == '\r') return;

  if (c == '\n') {
    _commitPartial();
    return;
  }

  if (c == '\b' || c == 0x7f) {
    if (_partialLine.length() > 0) _partialLine.remove(_partialLine.length() - 1);
    return;
  }

  if (c == '\t') {
    _partialLine += "    ";
  } else if (c >= 0x20 && c <= 0x7e) {
    _partialLine += (char)c;
  }
  // Other control/binary bytes are intentionally ignored in text-mode v1.

  if ((int)_partialLine.length() >= MAX_PARTIAL_LEN) _commitPartial();
}

void TelnetClientScreen::_commitPartial() {
  _pushOutputLine(_partialLine);
  _partialLine = "";
}

void TelnetClientScreen::_pushOutputLine(const String& line) {
  _transcript += line;
  _transcript += '\n';
  _trimTranscript();
}

void TelnetClientScreen::_trimTranscript() {
  if ((int)_transcript.length() <= MAX_TRANSCRIPT_CHARS) return;

  int excess = (int)_transcript.length() - MAX_TRANSCRIPT_CHARS;
  int cut = _transcript.indexOf('\n', excess);
  if (cut >= 0) _transcript.remove(0, cut + 1);
  else          _transcript.remove(0, excess);
}

void TelnetClientScreen::_clearOutput() {
  _transcript = "";
  _partialLine = "";
  _followOutput = true;
  _outputView.setContent("");
}

void TelnetClientScreen::_renderOutput() {
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

  // Fixed connection information at the top of every OUTPUT screen.
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
