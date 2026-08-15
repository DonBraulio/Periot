#pragma once

#include <Arduino.h>

#include "rf_protocol.h"

struct RfPositionUpdate {
  int16_t delta;
  bool newBoot;
};

void setupRfPositionTracker();
RfPositionUpdate trackRfPosition(const RfFrame& frame);
void updateRfPositionPersistence();
