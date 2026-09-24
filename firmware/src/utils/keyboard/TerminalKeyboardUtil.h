#pragma once

#include "utils/keyboard/HIDKeyboardUtil.h"

// Adapts the hotkey-capable virtual HID keyboard UI to a terminal byte stream.
// It intentionally reuses HID key semantics so the on-screen keyboard layout
// has a single source of truth across HID and Remote Access.
class TerminalKeyboardUtil : public HIDKeyboardUtil {
public:
  using WriteFn = bool (*)(void* context, const uint8_t* data, size_t len);

  TerminalKeyboardUtil(WriteFn writer, void* context)
    : _writer(writer), _context(context) { setDelayMs(0); }

  void begin() override {}
  void end() override {}
  bool isConnected() override { return _writer != nullptr; }
  void sendReport(KeyReport* report) override;
  void resetWriteStatus() override { _writeOk = true; }
  bool lastWriteOk() const override { return _writeOk; }

private:
  WriteFn _writer = nullptr;
  void* _context = nullptr;
  KeyReport _last = {};
  bool _writeOk = true;

  bool _send(const uint8_t* data, size_t len);
  bool _sendLiteral(const char* text);
  bool _emitKey(uint8_t usage, uint8_t modifiers);
  char _asciiForUsage(uint8_t usage, bool shift) const;
};
