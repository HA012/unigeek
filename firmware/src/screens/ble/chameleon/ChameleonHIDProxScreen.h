#pragma once
#include "ui/templates/BaseScreen.h"

class ChameleonHIDProxScreen : public BaseScreen {
public:
  enum Operation { READ_TAG, CLONE_TO_SLOT, WRITE_T5577 };
  explicit ChameleonHIDProxScreen(Operation operation = READ_TAG) : _operation(operation) {}
  const char* title() override { return "HID Prox"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT, STATE_CLONED, STATE_ERROR };
  State   _state     = STATE_IDLE;
  bool    _scanning  = false;
  bool    _needsDraw = true;
  Operation _operation = READ_TAG;

  uint8_t _payload[13] = {};
  uint8_t _payloadLen  = 0;

  void _draw();
  void _doScan();
  void _doCloneSlot();
  void _doT5577();
};
