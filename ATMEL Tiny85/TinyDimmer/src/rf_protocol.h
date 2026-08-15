#pragma once

#include <Arduino.h>
#include <rf_protocol_spec.h>

using RfFrame = RfProtocolSpec::Frame;

RfFrame createRfFrame(uint8_t nodeId, uint8_t bootId, uint8_t position);
void sendRfFrame(uint8_t txPin, const RfFrame& frame);
