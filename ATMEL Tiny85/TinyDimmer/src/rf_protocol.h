#pragma once

#include <Arduino.h>

struct RfFrame {
  uint8_t nodeId;
  int8_t direction;
  uint8_t sequence;
  uint16_t payload;
};

RfFrame createRfFrame(uint8_t nodeId, int8_t direction, uint8_t sequence);
void sendRfFrame(uint8_t txPin, const RfFrame& frame);
