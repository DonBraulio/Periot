#include "rf_protocol.h"

namespace {

constexpr uint8_t PREAMBLE_CYCLES = 16;
constexpr uint8_t PAYLOAD_BITS = 10;
constexpr uint16_t SHORT_US = 400;
constexpr uint16_t LONG_US = 1200;
constexpr uint16_t SYNC_LOW_US = 3000;

uint8_t calculateChecksum(uint8_t nodeId, uint8_t directionBit,
                          uint8_t sequence) {
  return (nodeId ^ (directionBit << 1) ^ (sequence << 2) ^ 0b101) & 0x07;
}

void sendPulse(uint8_t txPin, uint16_t highUs, uint16_t lowUs) {
  digitalWrite(txPin, HIGH);
  delayMicroseconds(highUs);
  digitalWrite(txPin, LOW);
  delayMicroseconds(lowUs);
}

}  // namespace

RfFrame createRfFrame(uint8_t nodeId, int8_t direction, uint8_t sequence) {
  const uint8_t normalizedNodeId = nodeId & 0x0F;
  const uint8_t directionBit = direction > 0 ? 1 : 0;
  const uint8_t normalizedSequence = sequence & 0x03;
  const uint8_t checksum =
      calculateChecksum(normalizedNodeId, directionBit, normalizedSequence);

  // Transmitted MSB first: node[3:0], direction, sequence[1:0], checksum[2:0].
  const uint16_t payload = (normalizedNodeId << 6) | (directionBit << 5) |
                           (normalizedSequence << 3) | checksum;

  const int8_t normalizedDirection = direction > 0 ? int8_t{1} : int8_t{-1};
  return {normalizedNodeId, normalizedDirection, normalizedSequence, payload};
}

void sendRfFrame(uint8_t txPin, const RfFrame& frame) {
  for (uint8_t i = 0; i < PREAMBLE_CYCLES; ++i) {
    sendPulse(txPin, SHORT_US, SHORT_US);
  }

  sendPulse(txPin, SHORT_US, SYNC_LOW_US);

  for (int8_t bit = PAYLOAD_BITS - 1; bit >= 0; --bit) {
    const bool isOne = frame.payload & (1U << bit);
    sendPulse(txPin, SHORT_US, isOne ? LONG_US : SHORT_US);
  }

  digitalWrite(txPin, LOW);
}
