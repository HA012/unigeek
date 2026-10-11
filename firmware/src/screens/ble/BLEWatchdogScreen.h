#pragma once
#include "BLEWatchScreenBase.h"

class BLEWatchdogScreen : public BLEWatchScreenBase {
public:
  BLEWatchdogScreen() : BLEWatchScreenBase(Mode::Watchdog) {}
};
