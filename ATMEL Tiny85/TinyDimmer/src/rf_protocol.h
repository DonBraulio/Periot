#pragma once

#include <Arduino.h>
#include <rf_protocol_spec.h>

using RfFrame = RfProtocolSpec::Frame;

RfFrame createRfFrame(uint8_t nodeId, int8_t direction, uint8_t sequence);
void sendRfFrame(uint8_t txPin, const RfFrame& frame);
