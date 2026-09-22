#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/ble/ChameleonClient.h"

// Darkside attack against a physical MIFARE Classic tag, using the
// Chameleon Ultra reader-mode command MF1_DARKSIDE_ACQUIRE (2004).
class ChameleonMfcDarksideScreen : public BaseScreen {
public:
  ChameleonMfcDarksideScreen() = default;
  ChameleonMfcDarksideScreen(const uint8_t* uid, uint8_t uidLen, uint8_t sectors,
                             const bool* foundA = nullptr,
                             const bool* foundB = nullptr);

  struct RecoveredKeyInfo {
    bool    valid  = false;
    uint8_t uid[7] = {};
    uint8_t uidLen = 0;
    uint8_t sector = 0;
    bool    keyB   = false;
    uint8_t key[6] = {};
  };
  static RecoveredKeyInfo takeRecoveredKey();

  const char* title() override { return "Darkside"; }
  bool inhibitPowerOff() override { return _busy; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { READY, RUNNING, SUCCESS, FAILED, ERROR };

  struct DarksideTarget {
    uint8_t sector = 0;
    uint8_t block  = 0;
    bool    keyB   = false;
  };

  uint8_t _uid[7] = {};
  uint8_t _uidLen = 0;
  uint8_t _sectors = 16;
  bool    _hasReadContext = false;
  bool    _foundA[40] = {};
  bool    _foundB[40] = {};
  DarksideTarget _target;
  bool    _hasTarget = false;
  State   _state = READY;
  bool    _busy  = false;

  uint8_t  _recoveredKey[6] = {};
  bool     _keyFound = false;
  String   _statusText;
  String   _keyHex;

  ScrollListView _scroll;
  ScrollListView::Row _rows[8];
  String _labels[8], _values[8];
  uint8_t _rowCount = 0;

  void _addRow(const char* label, const String& value);
  void _buildPreview();
  uint8_t _trailerBlock(uint8_t sector) const;
  bool _isMissing(uint8_t sector, bool keyB) const;
  bool _selectFirstMissing();
  bool _selectNextMissing(int direction);
  bool _validateTarget() const;
  bool _ensureTagContext();
  bool _tryCommonKeys(ChameleonClient& c, uint8_t keyType, uint8_t block);
  enum AttackResult { ATTACK_KEY_FOUND, ATTACK_NO_KEY, ATTACK_TAG_ABORT };
  AttackResult _attackCurrentTarget(ChameleonClient& c);
  bool _runSweep();
  void _saveKey();
  const char* _stateText() const;
  static const char* _statusName(uint8_t st);
};
