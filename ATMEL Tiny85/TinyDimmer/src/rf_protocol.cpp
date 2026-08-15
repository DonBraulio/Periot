#include "rf_protocol.h"

namespace {

void sendMarkAndSpace(uint8_t txPin, uint16_t spaceUs) {
  digitalWrite(txPin, HIGH);
  delayMicroseconds(RfProtocolSpec::MARK_US);
  digitalWrite(txPin, LOW);
  delayMicroseconds(spaceUs);
}

}  // namespace

RfFrame createRfFrame(uint8_t nodeId, int8_t direction, uint8_t sequence) {
  return RfProtocolSpec::createFrame(nodeId, direction, sequence);
}

void sendRfFrame(uint8_t txPin, const RfFrame& frame) {
  for (uint8_t i = 0; i < RfProtocolSpec::PREAMBLE_CYCLES; ++i) {
    sendMarkAndSpace(txPin, RfProtocolSpec::ZERO_SPACE_US);
  }

  sendMarkAndSpace(txPin, RfProtocolSpec::SYNC_SPACE_US);

  for (int8_t bit = RfProtocolSpec::PAYLOAD_BITS - 1; bit >= 0; --bit) {
    const bool isOne = frame.payload & (1U << bit);
    sendMarkAndSpace(txPin, isOne ? RfProtocolSpec::ONE_SPACE_US
                                  : RfProtocolSpec::ZERO_SPACE_US);
  }

  // This rising edge closes the final data space at the receiver.
  digitalWrite(txPin, HIGH);
  delayMicroseconds(RfProtocolSpec::STOP_MARK_US);
  digitalWrite(txPin, LOW);
}
