#pragma once

#include <stdint.h>

namespace RfProtocolSpec {

// Nominal wire timings. The receiver defines wider acceptance windows.
constexpr uint8_t PREAMBLE_CYCLES = 16;
constexpr uint8_t MIN_RECEIVED_PREAMBLE_CYCLES = 12;
constexpr uint8_t PAYLOAD_BITS = 18;
constexpr uint8_t DATA_BITS = 14;

constexpr uint16_t MARK_US = 400;
constexpr uint16_t ZERO_SPACE_US = 400;
constexpr uint16_t ONE_SPACE_US = 1200;
constexpr uint16_t SYNC_SPACE_US = 3000;
constexpr uint16_t STOP_MARK_US = 400;

constexpr uint8_t NODE_SHIFT = 14;
constexpr uint8_t BOOT_ID_SHIFT = 12;
constexpr uint8_t POSITION_SHIFT = 4;
constexpr uint32_t CRC_MASK = 0x0F;

struct Frame {
  uint8_t nodeId;
  uint8_t bootId;
  uint8_t position;
  uint32_t payload;
};

// CRC-4/ITU polynomial: x^4 + x + 1.
inline uint8_t calculateCrc(uint16_t data) {
  uint8_t crc = 0;

  for (int8_t bit = DATA_BITS - 1; bit >= 0; --bit) {
    const bool inputBit = (data >> bit) & 0x01;
    const bool feedback = ((crc >> 3) & 0x01) ^ inputBit;
    crc = (crc << 1) & CRC_MASK;
    if (feedback) {
      crc ^= 0x03;
    }
  }

  return crc;
}

inline Frame createFrame(uint8_t nodeId, uint8_t bootId,
                         uint8_t position) {
  const uint8_t normalizedNodeId = nodeId & 0x0F;
  const uint8_t normalizedBootId = bootId & 0x03;
  const uint16_t data = (normalizedNodeId << 10) |
                        (normalizedBootId << 8) | position;
  const uint32_t payload = (static_cast<uint32_t>(data) << 4) |
                           calculateCrc(data);

  return {normalizedNodeId, normalizedBootId, position, payload};
}

inline bool parseFrame(uint32_t payload, Frame& frame) {
  const uint16_t data = payload >> 4;
  const uint8_t receivedCrc = payload & CRC_MASK;

  if (receivedCrc != calculateCrc(data)) {
    return false;
  }

  frame = {
      static_cast<uint8_t>((payload >> NODE_SHIFT) & 0x0F),
      static_cast<uint8_t>((payload >> BOOT_ID_SHIFT) & 0x03),
      static_cast<uint8_t>((payload >> POSITION_SHIFT) & 0xFF),
      payload,
  };
  return true;
}

inline int16_t positionDelta(uint8_t current, uint8_t previous) {
  const uint8_t modularDelta = current - previous;
  return modularDelta <= 127 ? modularDelta
                             : static_cast<int16_t>(modularDelta) - 256;
}

}  // namespace RfProtocolSpec
