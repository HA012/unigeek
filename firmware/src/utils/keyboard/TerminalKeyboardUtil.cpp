#include "TerminalKeyboardUtil.h"
#include <cstring>

bool TerminalKeyboardUtil::_send(const uint8_t* data, size_t len) {
  bool ok = _writer && data && len && _writer(_context, data, len);
  if (!ok) _writeOk = false;
  return ok;
}

bool TerminalKeyboardUtil::_sendLiteral(const char* text) {
  return text && _send(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

char TerminalKeyboardUtil::_asciiForUsage(uint8_t u, bool shift) const {
  if (u >= 0x04 && u <= 0x1d) {
    char c = 'a' + (u - 0x04);
    return shift ? (char)(c - 'a' + 'A') : c;
  }
  static const char normal[]  = "1234567890-=[]\\;'`,./";
  static const char shifted[] = "!@#$%^&*()_+{}|:\"~<>?";
  if (u >= 0x1e && u <= 0x27) return shift ? shifted[u - 0x1e] : normal[u - 0x1e];
  switch (u) {
    case 0x2c: return ' ';
    case 0x2d: return shift ? '_' : '-';
    case 0x2e: return shift ? '+' : '=';
    case 0x2f: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default: return 0;
  }
}

bool TerminalKeyboardUtil::_emitKey(uint8_t u, uint8_t modifiers) {
  const bool ctrl  = (modifiers & 0x11) != 0;
  const bool shift = (modifiers & 0x22) != 0;
  const bool alt   = (modifiers & 0x44) != 0;
  const bool meta  = (modifiers & 0x88) != 0;

  const char* seq = nullptr;
  switch (u) {
    case 0x29: seq = "\x1b"; break;       // Esc
    case 0x2b: seq = "\t"; break;         // Tab
    case 0x28: seq = "\r\n"; break;       // Enter / SEND: preserve command-mode CRLF
    case 0x2a: seq = "\x7f"; break;       // Backspace
    case 0x4c: seq = "\x1b[3~"; break;    // Delete
    case 0x49: seq = "\x1b[2~"; break;    // Insert
    case 0x4a: seq = "\x1b[H"; break;     // Home
    case 0x4d: seq = "\x1b[F"; break;     // End
    case 0x4b: seq = "\x1b[5~"; break;    // Page Up
    case 0x4e: seq = "\x1b[6~"; break;    // Page Down
    case 0x4f: seq = "\x1b[C"; break;     // Right
    case 0x50: seq = "\x1b[D"; break;     // Left
    case 0x51: seq = "\x1b[B"; break;     // Down
    case 0x52: seq = "\x1b[A"; break;     // Up
    case 0x3a: seq = "\x1bOP"; break;     // F1
    case 0x3b: seq = "\x1bOQ"; break;
    case 0x3c: seq = "\x1bOR"; break;
    case 0x3d: seq = "\x1bOS"; break;
    case 0x3e: seq = "\x1b[15~"; break;
    case 0x3f: seq = "\x1b[17~"; break;
    case 0x40: seq = "\x1b[18~"; break;
    case 0x41: seq = "\x1b[19~"; break;
    case 0x42: seq = "\x1b[20~"; break;
    case 0x43: seq = "\x1b[21~"; break;
    case 0x44: seq = "\x1b[23~"; break;
    case 0x45: seq = "\x1b[24~"; break;
    default: break;
  }
  if (seq) return _sendLiteral(seq);

  char c = _asciiForUsage(u, shift);
  if (!c) return true;
  uint8_t out = (uint8_t)c;
  if (ctrl) {
    char upper = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    if (upper >= '@' && upper <= '_') out = (uint8_t)(upper & 0x1f);
  }
  if (alt || meta) {
    const uint8_t esc = 0x1b;
    if (!_send(&esc, 1)) return false;
  }
  return _send(&out, 1);
}

void TerminalKeyboardUtil::sendReport(KeyReport* report) {
  if (!report) return;
  // Emit only newly pressed keys. Release reports are deliberately silent.
  for (uint8_t i = 0; i < 6; ++i) {
    uint8_t key = report->keys[i];
    if (!key) continue;
    bool wasDown = false;
    for (uint8_t j = 0; j < 6; ++j) {
      if (_last.keys[j] == key) { wasDown = true; break; }
    }
    if (!wasDown) _emitKey(key, report->modifiers);
  }
  _last = *report;
}
