#pragma once

#include <Arduino.h>

constexpr uint8_t RF_PAYLOAD_BITS = 10;

struct RfFrame {
  uint8_t nodeId;
  int8_t direction;
  uint8_t sequence;
  uint16_t payload;
};

void setupRfReceiver(uint8_t rxPin);
bool receiveRfFrame(RfFrame& frame);
void printRfDiagnostics();
