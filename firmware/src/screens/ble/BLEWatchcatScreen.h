#pragma once
#include "BLEWatchScreenBase.h"

class BLEWatchcatScreen : public BLEWatchScreenBase {
public:
  BLEWatchcatScreen() : BLEWatchScreenBase(Mode::Watchcat) {}
};
