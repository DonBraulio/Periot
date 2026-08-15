#pragma once

#include <stdint.h>

namespace RfProtocolSpec {

// Nominal wire timings. The receiver defines wider acceptance windows.
constexpr uint8_t PREAMBLE_CYCLES = 16;
constexpr uint8_t MIN_RECEIVED_PREAMBLE_CYCLES = 12;
constexpr uint8_t PAYLOAD_BITS = 10;

constexpr uint16_t MARK_US = 400;
constexpr uint16_t ZERO_SPACE_US = 400;
constexpr uint16_t ONE_SPACE_US = 1200;
constexpr uint16_t SYNC_SPACE_US = 3000;
constexpr uint16_t STOP_MARK_US = 400;

constexpr uint8_t NODE_SHIFT = 6;
constexpr uint8_t DIRECTION_SHIFT = 5;
constexpr uint8_t SEQUENCE_SHIFT = 3;

struct Frame {
  uint8_t nodeId;
  int8_t direction;
  uint8_t sequence;
  uint16_t payload;
};

inline uint8_t calculateChecksum(uint8_t nodeId, uint8_t directionBit,
                                 uint8_t sequence) {
  return (nodeId ^ (directionBit << 1) ^ (sequence << 2) ^ 0b101) & 0x07;
}

inline Frame createFrame(uint8_t nodeId, int8_t direction, uint8_t sequence) {
  const uint8_t normalizedNodeId = nodeId & 0x0F;
  const uint8_t directionBit = direction > 0 ? 1 : 0;
  const uint8_t normalizedSequence = sequence & 0x03;
  const int8_t normalizedDirection = directionBit ? int8_t{1} : int8_t{-1};
  const uint8_t checksum = calculateChecksum(
      normalizedNodeId, directionBit, normalizedSequence);

  const uint16_t payload =
      (normalizedNodeId << NODE_SHIFT) |
      (directionBit << DIRECTION_SHIFT) |
      (normalizedSequence << SEQUENCE_SHIFT) | checksum;

  return {normalizedNodeId, normalizedDirection, normalizedSequence, payload};
}

inline bool parseFrame(uint16_t payload, Frame& frame) {
  const uint8_t nodeId = (payload >> NODE_SHIFT) & 0x0F;
  const uint8_t directionBit = (payload >> DIRECTION_SHIFT) & 0x01;
  const uint8_t sequence = (payload >> SEQUENCE_SHIFT) & 0x03;
  const uint8_t receivedChecksum = payload & 0x07;

  if (receivedChecksum !=
      calculateChecksum(nodeId, directionBit, sequence)) {
    return false;
  }

  const int8_t direction = directionBit ? int8_t{1} : int8_t{-1};
  frame = {nodeId, direction, sequence, payload};
  return true;
}

}  // namespace RfProtocolSpec
